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

#include "duckdb/common/printer.hpp"


namespace duckdb {

void ClickhouseConversion::ConvertValidity(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset,
                                           idx_t count) {
	// Check if column is Nullable wrapper
	auto *nullable = dynamic_cast<clickhouse::ColumnNullable *>(ch_column.get());
	if (!nullable) {
		return; // Not nullable, all valid
	}

	auto &validity = FlatVector::Validity(output);
	auto nulls = nullable->Nulls()->As<clickhouse::ColumnUInt8>();
	if (!nulls) {
		throw InternalException("Unexpected ClickHouse null-map column type");
	}
	auto &null_data = nulls->GetWritableData();
	D_ASSERT(offset + count <= null_data.size());

	for (idx_t i = 0; i < count; i++) {
		auto is_valid = null_data[offset + i] == 0;
		validity.Set(i, is_valid);		
	}
}

template <typename CH_TYPE, typename DUCK_TYPE>
static void CopyNumericColumnTemplated(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset, idx_t count) {
	auto ch_typed = ch_column->As<clickhouse::ColumnVector<CH_TYPE>>();
	auto duck_data = FlatVector::GetData<DUCK_TYPE>(output);

	for (idx_t i = 0; i < count; i++) {
		duck_data[i] = static_cast<DUCK_TYPE>((*ch_typed)[offset + i]);
	}
}

template <typename NUMERIC_TYPE>
static void ReferenceNumericColumnTemplated(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset,
                                            idx_t count) {
	auto ch_typed = ch_column->As<clickhouse::ColumnVector<NUMERIC_TYPE>>();
	if (!ch_typed) {
		throw InternalException("Unexpected ClickHouse column type for zero-copy numeric conversion");
	}

	auto &ch_data = ch_typed->GetWritableData();
	D_ASSERT(offset + count <= ch_data.size());
	if (output.GetBuffer()) {
		output.GetBuffer()->SetAuxiliaryData(make_uniq<ClickhouseAuxiliaryData>(ch_typed));
	}
	FlatVector::SetData(output, reinterpret_cast<data_ptr_t>(ch_data.data() + offset));
}

void ClickhouseConversion::ConvertNumericColumn(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset,
                                                idx_t count, const LogicalType &type) {
	// Handle nullable wrapper
	auto *nullable = dynamic_cast<clickhouse::ColumnNullable *>(ch_column.get());
	auto nullable_column = ch_column;
	if (nullable) {
		ch_column = nullable->Nested();
	}

	switch (type.id()) {
	case LogicalTypeId::BOOLEAN:
		// ClickHouse represents Bool as UInt8, which cannot safely be exposed as a C++ bool array.
		CopyNumericColumnTemplated<uint8_t, bool>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::TINYINT:
		ReferenceNumericColumnTemplated<int8_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::UTINYINT:
		ReferenceNumericColumnTemplated<uint8_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::SMALLINT:
		ReferenceNumericColumnTemplated<int16_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::USMALLINT:
		ReferenceNumericColumnTemplated<uint16_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::INTEGER:
		ReferenceNumericColumnTemplated<int32_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::UINTEGER:
		ReferenceNumericColumnTemplated<uint32_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::BIGINT:
		ReferenceNumericColumnTemplated<int64_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::UBIGINT:
		ReferenceNumericColumnTemplated<uint64_t>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::FLOAT:
		ReferenceNumericColumnTemplated<float>(ch_column, output, offset, count);
		break;
	case LogicalTypeId::DOUBLE:
		ReferenceNumericColumnTemplated<double>(ch_column, output, offset, count);
		break;
	default:
		throw NotImplementedException("Unsupported numeric type for conversion");
	}

	// Handle nullability after data conversion
	if (nullable) {
		ConvertValidity(nullable_column, output, offset, count);
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

		// // Handle row_id column
		// if (col_idx == COLUMN_IDENTIFIER_ROW_ID) {
		// 	auto &vec = output.data[output_idx];y
		// 	vec.SetVectorType(VectorType::FLAT_VECTOR);
		// 	auto data = FlatVector::GetData<row_t>(vec);
		// 	for (idx_t i = 0; i < count; i++) {
		// 		data[i] = static_cast<row_t>(block_offset + i);
		// 	}
		// 	continue;
		// }

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
