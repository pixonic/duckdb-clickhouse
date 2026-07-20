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

void ConvertValidity(clickhouse::ColumnNullable *nullable, Vector &output, idx_t offset, idx_t count) {
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

template <typename TYPE>
void ConvertDirect(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset, idx_t count) {
	auto ch_typed = ch_column->As<clickhouse::ColumnVector<TYPE>>();
	if (!ch_typed) {
		throw InternalException("Unexpected ClickHouse column type for zero-copy conversion");
	}

	auto &ch_data = ch_typed->GetWritableData();
	D_ASSERT(offset + count <= ch_data.size());
	if (output.GetBuffer()) {
		output.GetBuffer()->SetAuxiliaryData(make_uniq<ClickhouseAuxiliaryData>(ch_typed));
	}
	FlatVector::SetData(output, reinterpret_cast<data_ptr_t>(ch_data.data() + offset));
}

template <typename COLUMN_TYPE>
void ConvertStringColumn(const std::shared_ptr<COLUMN_TYPE> &ch_string_col, Vector &output, idx_t offset, idx_t count) {
	auto result_data = FlatVector::GetData<string_t>(output);

	if (output.GetBuffer()) {
		output.GetBuffer()->SetAuxiliaryData(make_uniq<ClickhouseAuxiliaryData>(ch_string_col));
	}

	for (idx_t i = 0; i < count; i++) {
		auto ch_str = ch_string_col->At(offset + i);
		result_data[i] = string_t(ch_str.data(), UnsafeNumericCast<uint32_t>(ch_str.size()));
	}
}

void ConvertString(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset, idx_t count) {
	auto ch_string_col = ch_column->As<clickhouse::ColumnString>();
	if (ch_string_col) {
		ConvertStringColumn(ch_string_col, output, offset, count);
		return;
	}

	auto ch_fixed_string_col = ch_column->As<clickhouse::ColumnFixedString>();
	if (ch_fixed_string_col) {
		ConvertStringColumn(ch_fixed_string_col, output, offset, count);
		return;
	}

	throw InternalException("Unexpected ClickHouse string column type");
}

void ConvertDate(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset, idx_t count) {
	auto ch_date = ch_column->As<clickhouse::ColumnDate>();
	auto result_data = FlatVector::GetData<date_t>(output);

	for (idx_t i = 0; i < count; i++) {
		// ClickHouse Date is stored as days since Unix epoch (same as DuckDB).
		auto days = ch_date->RawAt(offset + i);
		result_data[i] = date_t(UnsafeNumericCast<int32_t>(days));
	}
}

void ConvertTimestamp(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset, idx_t count) {
	auto ch_datetime = ch_column->As<clickhouse::ColumnDateTime>();
	auto result_data = FlatVector::GetData<timestamp_t>(output);

	for (idx_t i = 0; i < count; i++) {
		// ClickHouse DateTime = Unix timestamp (seconds since epoch)
		// DuckDB TIMESTAMP = microseconds since epoch
		auto unix_ts = ch_datetime->At(offset + i);
		result_data[i] = Timestamp::FromEpochSeconds(UnsafeNumericCast<int64_t>(unix_ts));
	}
}

void ClickhouseConversion::BlockToDuckDB(clickhouse::Block &block, DataChunk &output, idx_t block_offset, idx_t count) {
	output.SetCardinality(count);

	for (idx_t i = 0; i < output.ColumnCount(); i++) {
		auto &vector = output.data[i];
		vector.SetVectorType(VectorType::FLAT_VECTOR);

		auto ch_column = block[i];
		auto *nullable = dynamic_cast<clickhouse::ColumnNullable *>(ch_column.get());
		auto nested_column = ch_column;
		if (nullable) {
			nested_column = nullable->Nested();
			ConvertValidity(nullable, vector, block_offset, count);
		}

		auto type = vector.GetType();

		// Convert based on type
		switch (type.id()) {
		case LogicalTypeId::BOOLEAN:
		case LogicalTypeId::UTINYINT:
			ConvertDirect<uint8_t>(nested_column, vector, block_offset, count);
			break;
		case LogicalTypeId::TINYINT:
			ConvertDirect<int8_t>(nested_column, vector, block_offset, count);
			break;
		case LogicalTypeId::SMALLINT:
			ConvertDirect<int16_t>(nested_column, vector, block_offset, count);
			break;
		case LogicalTypeId::USMALLINT:
			ConvertDirect<uint16_t>(nested_column, vector, block_offset, count);
			break;
		case LogicalTypeId::INTEGER:
			ConvertDirect<int32_t>(nested_column, vector, block_offset, count);
			break;
		case LogicalTypeId::UINTEGER:
			ConvertDirect<uint32_t>(nested_column, vector, block_offset, count);
			break;
		case LogicalTypeId::BIGINT:
			ConvertDirect<int64_t>(nested_column, vector, block_offset, count);
			break;
		case LogicalTypeId::UBIGINT:
			ConvertDirect<uint64_t>(nested_column, vector, block_offset, count);
			break;
		case LogicalTypeId::FLOAT:
			ConvertDirect<float>(nested_column, vector, block_offset, count);
			break;
		case LogicalTypeId::DOUBLE:
			ConvertDirect<double>(nested_column, vector, block_offset, count);
			break;
		case LogicalTypeId::VARCHAR:
			ConvertString(nested_column, vector, block_offset, count);
			break;
		case LogicalTypeId::DATE:
			ConvertDate(nested_column, vector, block_offset, count);
			break;
		case LogicalTypeId::TIMESTAMP:
			ConvertTimestamp(nested_column, vector, block_offset, count);
			break;
		default:
			throw NotImplementedException("Unsupported type for ClickHouse conversion: " + type.ToString());
		}
	}
}

} // namespace duckdb
