#include "clickhouse_conversion.hpp"
#include "duckdb/common/exception/conversion_exception.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/interval.hpp"
#include "duckdb/common/types/time.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/string_type.hpp"
#include "duckdb/common/operator/multiply.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"

#include <clickhouse/columns/column.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <clickhouse/columns/date.h>
#include <clickhouse/columns/time.h>
#include <clickhouse/columns/nullable.h>
#include <clickhouse/columns/lowcardinality.h>

#include <limits>

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
	if (ch_date) {
		for (idx_t i = 0; i < count; i++) {
			// ClickHouse Date is stored as days since Unix epoch (same as DuckDB).
			auto days = ch_date->RawAt(offset + i);
			result_data[i] = date_t(UnsafeNumericCast<int32_t>(days));
		}
		return;
	}

	auto ch_date32 = ch_column->As<clickhouse::ColumnDate32>();
	if (ch_date32) {
		for (idx_t i = 0; i < count; i++) {
			// ClickHouse Date32 and DuckDB DATE both store signed days since Unix epoch.
			result_data[i] = date_t(ch_date32->RawAt(offset + i));
		}
		return;
	}

	throw InternalException("Unexpected ClickHouse date column type");
}

static int64_t PowerOfTen(idx_t exponent) {
	static constexpr int64_t POWERS_OF_TEN[] = {1,      10,      100,      1000,      10000,
	                                            100000, 1000000, 10000000, 100000000, 1000000000};
	D_ASSERT(exponent < sizeof(POWERS_OF_TEN) / sizeof(POWERS_OF_TEN[0]));
	return POWERS_OF_TEN[exponent];
}

static int64_t ScaleTemporalTicks(int64_t value, idx_t source_precision, idx_t target_precision,
                                  const string &source_type, const LogicalType &target_type) {
	if (source_precision > target_precision) {
		throw InternalException("Cannot exactly scale ClickHouse %s to DuckDB %s", source_type, target_type.ToString());
	}

	int64_t result;
	auto multiplier = PowerOfTen(target_precision - source_precision);
	if (!TryMultiplyOperator::Operation(value, multiplier, result) || !Timestamp::IsFinite(timestamp_t(result))) {
		throw ConversionException(
		    "Cannot exactly convert ClickHouse %s value %d to DuckDB %s: value is outside the supported range",
		    source_type, value, target_type.ToString());
	}
	return result;
}

template <typename TARGET_TYPE>
void ConvertDateTime64(const std::shared_ptr<clickhouse::ColumnDateTime64> &ch_datetime, Vector &output, idx_t offset,
                       idx_t count, idx_t target_precision) {
	auto result_data = FlatVector::GetData<TARGET_TYPE>(output);
	auto source_precision = ch_datetime->GetPrecision();
	auto source_type = ch_datetime->Type()->GetName();

	for (idx_t i = 0; i < count; i++) {
		auto ticks = ch_datetime->At(offset + i);
		auto scaled_ticks =
		    ScaleTemporalTicks(ticks, source_precision, target_precision, source_type, output.GetType());
		result_data[i] = TARGET_TYPE(scaled_ticks);
	}
}

void ConvertTimestamp(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset, idx_t count) {
	auto ch_datetime = ch_column->As<clickhouse::ColumnDateTime>();
	if (ch_datetime) {
		if (output.GetType().id() != LogicalTypeId::TIMESTAMP) {
			throw InternalException("Unexpected DuckDB type for ClickHouse DateTime conversion: " +
			                        output.GetType().ToString());
		}
		auto result_data = FlatVector::GetData<timestamp_t>(output);
		for (idx_t i = 0; i < count; i++) {
			// ClickHouse DateTime = Unix timestamp (seconds since epoch)
			// DuckDB TIMESTAMP = microseconds since epoch
			auto unix_ts = ch_datetime->At(offset + i);
			result_data[i] = Timestamp::FromEpochSeconds(UnsafeNumericCast<int64_t>(unix_ts));
		}
		return;
	}

	auto ch_datetime64 = ch_column->As<clickhouse::ColumnDateTime64>();
	if (!ch_datetime64) {
		throw InternalException("Unexpected ClickHouse timestamp column type");
	}

	switch (output.GetType().id()) {
	case LogicalTypeId::TIMESTAMP_SEC:
		ConvertDateTime64<timestamp_sec_t>(ch_datetime64, output, offset, count, 0);
		break;
	case LogicalTypeId::TIMESTAMP_MS:
		ConvertDateTime64<timestamp_ms_t>(ch_datetime64, output, offset, count, 3);
		break;
	case LogicalTypeId::TIMESTAMP:
		ConvertDateTime64<timestamp_t>(ch_datetime64, output, offset, count, 6);
		break;
	case LogicalTypeId::TIMESTAMP_NS:
		ConvertDateTime64<timestamp_ns_t>(ch_datetime64, output, offset, count, 9);
		break;
	default:
		throw InternalException("Unexpected DuckDB timestamp type: " + output.GetType().ToString());
	}
}

template <typename TARGET_TYPE>
void ConvertTime(clickhouse::ColumnRef ch_column, Vector &output, idx_t offset, idx_t count, idx_t target_precision) {
	idx_t source_precision;
	string source_type;
	auto ch_time = ch_column->As<clickhouse::ColumnTime>();
	auto ch_time64 = ch_column->As<clickhouse::ColumnTime64>();
	if (ch_time) {
		source_precision = 0;
		source_type = ch_time->Type()->GetName();
	} else if (ch_time64) {
		source_precision = ch_time64->GetPrecision();
		source_type = ch_time64->Type()->GetName();
	} else {
		throw InternalException("Unexpected ClickHouse time column type");
	}

	auto max_source_ticks = Interval::SECS_PER_DAY * PowerOfTen(source_precision);
	auto result_data = FlatVector::GetData<TARGET_TYPE>(output);
	for (idx_t i = 0; i < count; i++) {
		int64_t source_ticks = ch_time ? ch_time->At(offset + i) : ch_time64->At(offset + i);
		if (source_ticks < 0 || source_ticks > max_source_ticks) {
			throw ConversionException(
			    "Cannot convert ClickHouse %s value %d to DuckDB %s: time must be between 00:00:00 and 24:00:00",
			    source_type, source_ticks, output.GetType().ToString());
		}
		auto scaled_ticks =
		    ScaleTemporalTicks(source_ticks, source_precision, target_precision, source_type, output.GetType());
		result_data[i] = TARGET_TYPE(scaled_ticks);
	}
}

template <typename INDEX_TYPE>
bool ConvertLowCardinalityIndexes(clickhouse::ColumnRef index_column, SelectionVector &selection, idx_t offset,
                                  idx_t count, idx_t dictionary_size) {
	auto typed_indexes = index_column->As<clickhouse::ColumnVector<INDEX_TYPE>>();
	if (!typed_indexes) {
		return false;
	}

	auto &indexes = typed_indexes->GetWritableData();
	D_ASSERT(offset + count <= indexes.size());

	for (idx_t i = 0; i < count; i++) {
		auto dictionary_index = static_cast<uint64_t>(indexes[offset + i]);
		if (dictionary_index >= dictionary_size || dictionary_index > std::numeric_limits<sel_t>::max()) {
			throw InternalException("Invalid ClickHouse LowCardinality dictionary index");
		}
		selection.set_index(i, static_cast<sel_t>(dictionary_index));
	}
	return true;
}

void ConvertLowCardinality(const std::shared_ptr<clickhouse::ColumnLowCardinality> &low_cardinality, Vector &output,
                           idx_t offset, idx_t count) {
	auto dictionary_size = UnsafeNumericCast<idx_t>(low_cardinality->GetDictionarySize());
	auto dictionary_column = low_cardinality->GetDictionaryColumn();
	auto dictionary_values = dictionary_column;

	auto *nullable_dictionary = dynamic_cast<clickhouse::ColumnNullable *>(dictionary_column.get());
	if (nullable_dictionary) {
		dictionary_values = nullable_dictionary->Nested();
	}
	if (!dictionary_values->As<clickhouse::ColumnString>()) {
		throw NotImplementedException("Unsupported ClickHouse LowCardinality type: " +
		                              low_cardinality->Type()->GetName());
	}

	Vector dictionary(output.GetType(), dictionary_size);
	ConvertString(dictionary_values, dictionary, 0, dictionary_size);
	if (nullable_dictionary) {
		ConvertValidity(nullable_dictionary, dictionary, 0, dictionary_size);
	}

	SelectionVector selection(count);
	auto index_column = low_cardinality->GetIndexColumn();
	auto converted = ConvertLowCardinalityIndexes<uint8_t>(index_column, selection, offset, count, dictionary_size) ||
	                 ConvertLowCardinalityIndexes<uint16_t>(index_column, selection, offset, count, dictionary_size) ||
	                 ConvertLowCardinalityIndexes<uint32_t>(index_column, selection, offset, count, dictionary_size) ||
	                 ConvertLowCardinalityIndexes<uint64_t>(index_column, selection, offset, count, dictionary_size);
	if (!converted) {
		throw InternalException("Unexpected ClickHouse LowCardinality index column type");
	}

	output.Dictionary(dictionary, dictionary_size, selection, count);
}

void ColumnToDuckDB(clickhouse::ColumnRef ch_column, Vector &vector, idx_t offset, idx_t count) {
	vector.SetVectorType(VectorType::FLAT_VECTOR);

	auto low_cardinality = ch_column->As<clickhouse::ColumnLowCardinality>();
	if (low_cardinality) {
		if (vector.GetType().id() != LogicalTypeId::VARCHAR) {
			throw NotImplementedException("Unsupported ClickHouse LowCardinality type: " +
			                              low_cardinality->Type()->GetName());
		}
		ConvertLowCardinality(low_cardinality, vector, offset, count);
		return;
	}

	auto *nullable = dynamic_cast<clickhouse::ColumnNullable *>(ch_column.get());
	auto nested_column = ch_column;
	if (nullable) {
		nested_column = nullable->Nested();
		ConvertValidity(nullable, vector, offset, count);
	}

	auto type = vector.GetType();

	// Convert based on type
	switch (type.id()) {
	case LogicalTypeId::BOOLEAN:
	case LogicalTypeId::UTINYINT:
		ConvertDirect<uint8_t>(nested_column, vector, offset, count);
		break;
	case LogicalTypeId::TINYINT:
		ConvertDirect<int8_t>(nested_column, vector, offset, count);
		break;
	case LogicalTypeId::SMALLINT:
		ConvertDirect<int16_t>(nested_column, vector, offset, count);
		break;
	case LogicalTypeId::USMALLINT:
		ConvertDirect<uint16_t>(nested_column, vector, offset, count);
		break;
	case LogicalTypeId::INTEGER:
		ConvertDirect<int32_t>(nested_column, vector, offset, count);
		break;
	case LogicalTypeId::UINTEGER:
		ConvertDirect<uint32_t>(nested_column, vector, offset, count);
		break;
	case LogicalTypeId::BIGINT:
		ConvertDirect<int64_t>(nested_column, vector, offset, count);
		break;
	case LogicalTypeId::UBIGINT:
		ConvertDirect<uint64_t>(nested_column, vector, offset, count);
		break;
	case LogicalTypeId::FLOAT:
		ConvertDirect<float>(nested_column, vector, offset, count);
		break;
	case LogicalTypeId::DOUBLE:
		ConvertDirect<double>(nested_column, vector, offset, count);
		break;
	case LogicalTypeId::VARCHAR:
		ConvertString(nested_column, vector, offset, count);
		break;
	case LogicalTypeId::DATE:
		ConvertDate(nested_column, vector, offset, count);
		break;
	case LogicalTypeId::TIMESTAMP:
	case LogicalTypeId::TIMESTAMP_SEC:
	case LogicalTypeId::TIMESTAMP_MS:
	case LogicalTypeId::TIMESTAMP_NS:
		ConvertTimestamp(nested_column, vector, offset, count);
		break;
	case LogicalTypeId::TIME:
		ConvertTime<dtime_t>(nested_column, vector, offset, count, 6);
		break;
	case LogicalTypeId::TIME_NS:
		ConvertTime<dtime_ns_t>(nested_column, vector, offset, count, 9);
		break;
	default:
		throw NotImplementedException("Unsupported type for ClickHouse conversion: " + type.ToString());
	}
}

void ClickhouseConversion::BlockToDuckDB(clickhouse::Block &block, DataChunk &output, idx_t block_offset, idx_t count) {
	output.SetCardinality(count);

	for (idx_t i = 0; i < output.ColumnCount(); i++) {
		auto ch_column = block[i];
		auto &vector = output.data[i];
		ColumnToDuckDB(ch_column, vector, block_offset, count);
	}
}

} // namespace duckdb
