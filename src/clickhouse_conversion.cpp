#include "clickhouse_conversion.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/string_type.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"

#include <clickhouse/columns/column.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <clickhouse/columns/date.h>
#include <clickhouse/columns/nullable.h>

namespace duckdb {

void ClickhouseConversion::ConvertValidity(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset,
                                           idx_t count) {
	// Check if column is Nullable wrapper
	auto *nullable = dynamic_cast<clickhouse::ColumnNullable *>(ch_column.get());
	if (!nullable) {
		return; // Not nullable, all valid
	}

	auto &validity = FlatVector::Validity(output);
	validity.EnsureWritable();

	for (idx_t i = 0; i < count; i++) {
		if (nullable->IsNull(offset + i)) {
			validity.SetInvalid(i);
		}
	}
}

template <typename CH_TYPE, typename DUCK_TYPE>
static void ConvertNumericColumnTemplated(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset, idx_t count) {
	auto ch_typed = ch_column->As<clickhouse::ColumnVector<CH_TYPE>>();
	auto duck_data = FlatVector::GetData<DUCK_TYPE>(output);

	// Direct memory copy for compatible types
	for (idx_t i = 0; i < count; i++) {
		duck_data[i] = static_cast<DUCK_TYPE>((*ch_typed)[offset + i]);
	}
}

void ClickhouseConversion::ConvertNumericColumn(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset,
                                                idx_t count, const LogicalType &type) {
	// Handle nullable wrapper
	auto *nullable = dynamic_cast<clickhouse::ColumnNullable *>(ch_column.get());
	if (nullable) {
		ch_column = nullable->Nested();
	}

	switch (type.id()) {
	case LogicalTypeId::BOOLEAN:
		ConvertNumericColumnTemplated<uint8_t, bool>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::TINYINT:
		ConvertNumericColumnTemplated<int8_t, int8_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::UTINYINT:
		ConvertNumericColumnTemplated<uint8_t, uint8_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::SMALLINT:
		ConvertNumericColumnTemplated<int16_t, int16_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::USMALLINT:
		ConvertNumericColumnTemplated<uint16_t, uint16_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::INTEGER:
		ConvertNumericColumnTemplated<int32_t, int32_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::UINTEGER:
		ConvertNumericColumnTemplated<uint32_t, uint32_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::BIGINT:
		ConvertNumericColumnTemplated<int64_t, int64_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::UBIGINT:
		ConvertNumericColumnTemplated<uint64_t, uint64_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::FLOAT:
		ConvertNumericColumnTemplated<float, float>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::DOUBLE:
		ConvertNumericColumnTemplated<double, double>(ch_column, output, offset, count);
		break;
	default:
		throw NotImplementedException("Unsupported numeric type for conversion");
	}

	// Handle nullability after data conversion
	if (nullable) {
		ConvertValidity(dynamic_cast<clickhouse::ColumnNullable *>(ch_column.get())->shared_from_this(), output,
		                offset, count);
	}
}

void ClickhouseConversion::ConvertStringColumn(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset,
                                               idx_t count) {
	// Handle nullable wrapper
	auto *nullable = dynamic_cast<clickhouse::ColumnNullable *>(ch_column.get());
	clickhouse::ColumnRef nested_column = ch_column;
	if (nullable) {
		nested_column = nullable->Nested();
	}

	auto ch_string_col = nested_column->As<clickhouse::ColumnString>();
	auto result_data = FlatVector::GetData<string_t>(output);

	for (idx_t i = 0; i < count; i++) {
		auto ch_str = ch_string_col->At(offset + i);
		result_data[i] = StringVector::AddString(output, ch_str.data(), ch_str.size());
	}

	if (nullable) {
		ConvertValidity(ch_column, output, offset, count);
	}
}

void ClickhouseConversion::ConvertDateColumn(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset,
                                             idx_t count) {
	// Handle nullable wrapper
	auto *nullable = dynamic_cast<clickhouse::ColumnNullable *>(ch_column.get());
	clickhouse::ColumnRef nested_column = ch_column;
	if (nullable) {
		nested_column = nullable->Nested();
	}

	auto ch_date = nested_column->As<clickhouse::ColumnDate>();
	auto result_data = FlatVector::GetData<date_t>(output);

	for (idx_t i = 0; i < count; i++) {
		// ClickHouse Date is days since Unix epoch (same as DuckDB!)
		auto days = ch_date->At(offset + i);
		result_data[i] = date_t(static_cast<int32_t>(days));
	}

	if (nullable) {
		ConvertValidity(ch_column, output, offset, count);
	}
}

void ClickhouseConversion::ConvertDateTimeColumn(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset,
                                                 idx_t count) {
	// Handle nullable wrapper
	auto *nullable = dynamic_cast<clickhouse::ColumnNullable *>(ch_column.get());
	clickhouse::ColumnRef nested_column = ch_column;
	if (nullable) {
		nested_column = nullable->Nested();
	}

	auto ch_datetime = nested_column->As<clickhouse::ColumnDateTime>();
	auto result_data = FlatVector::GetData<timestamp_t>(output);

	for (idx_t i = 0; i < count; i++) {
		// ClickHouse DateTime = Unix timestamp (seconds since epoch)
		// DuckDB TIMESTAMP = microseconds since epoch
		auto unix_ts = ch_datetime->At(offset + i);
		result_data[i] = Timestamp::FromEpochSeconds(static_cast<int64_t>(unix_ts));
	}

	if (nullable) {
		ConvertValidity(ch_column, output, offset, count);
	}
}

void ClickhouseConversion::BlockToDuckDB(clickhouse::Block &block, DataChunk &output, idx_t block_offset, idx_t count,
                                         const vector<column_t> &column_ids, const vector<LogicalType> &column_types) {
	output.SetCardinality(count);

	for (idx_t output_idx = 0; output_idx < column_ids.size(); output_idx++) {
		auto col_idx = column_ids[output_idx];

		// Handle row_id column
		if (col_idx == COLUMN_IDENTIFIER_ROW_ID) {
			auto &vec = output.data[output_idx];
			vec.SetVectorType(VectorType::FLAT_VECTOR);
			auto data = FlatVector::GetData<row_t>(vec);
			for (idx_t i = 0; i < count; i++) {
				data[i] = static_cast<row_t>(block_offset + i);
			}
			continue;
		}

		// Get ClickHouse column
		auto ch_column = block[col_idx];
		auto &output_vector = output.data[output_idx];
		output_vector.SetVectorType(VectorType::FLAT_VECTOR);

		// Get the DuckDB type for this column
		const auto &duck_type = column_types[col_idx];

		// Convert based on type
		switch (duck_type.id()) {
		case LogicalTypeId::BOOLEAN:
		case LogicalTypeId::TINYINT:
		case LogicalTypeId::UTINYINT:
		case LogicalTypeId::SMALLINT:
		case LogicalTypeId::USMALLINT:
		case LogicalTypeId::INTEGER:
		case LogicalTypeId::UINTEGER:
		case LogicalTypeId::BIGINT:
		case LogicalTypeId::UBIGINT:
		case LogicalTypeId::FLOAT:
		case LogicalTypeId::DOUBLE:
			ConvertNumericColumn(ch_column, output_vector, block_offset, count, duck_type);
			break;
		case LogicalTypeId::VARCHAR:
			ConvertStringColumn(ch_column, output_vector, block_offset, count);
			break;
		case LogicalTypeId::DATE:
			ConvertDateColumn(ch_column, output_vector, block_offset, count);
			break;
		case LogicalTypeId::TIMESTAMP:
			ConvertDateTimeColumn(ch_column, output_vector, block_offset, count);
			break;
		default:
			throw NotImplementedException("Unsupported type for ClickHouse conversion: " + duck_type.ToString());
		}
	}
}

} // namespace duckdb
