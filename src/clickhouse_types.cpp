#include "clickhouse_types.hpp"

namespace duckdb {

LogicalType ClickhouseTypes::TypeToLogicalType(const ClickhouseTypeData &input) {
	auto name = input.type;

	// TODO support other types

	if (StringUtil::Contains(name, "Bool")) {
		return LogicalType::BOOLEAN;
	} else if (StringUtil::Contains(name, "UInt8")) {
		return LogicalType::UTINYINT;
	} else if (StringUtil::Contains(name, "Int8")) {
		return LogicalType::TINYINT;
	} else if (StringUtil::Contains(name, "UInt16")) {
		return LogicalType::USMALLINT;
	} else if (StringUtil::Contains(name, "Int16")) {
		return LogicalType::SMALLINT;
	} else if (StringUtil::Contains(name, "UInt32")) {
		return LogicalType::UINTEGER;
	} else if (StringUtil::Contains(name, "Int32")) {
		return LogicalType::INTEGER;
	} else if (StringUtil::Contains(name, "UInt64")) {
		return LogicalType::UBIGINT;
	} else if (StringUtil::Contains(name, "Int64")) {
		return LogicalType::BIGINT;
	} else if (StringUtil::Contains(name, "Float32")) {
		return LogicalType::FLOAT;
	} else if (StringUtil::Contains(name, "Float64")) {
		return LogicalType::DOUBLE;
	} else if (StringUtil::Contains(name, "String") || StringUtil::Contains(name, "FixedString")) {
		return LogicalType::VARCHAR;
	} else if (StringUtil::Contains(name, "Date")) {
		return LogicalType::DATE;
	} else if (StringUtil::Contains(name, "DateTime")) {
		return LogicalType::TIMESTAMP;
	} else {
		throw InternalException("Unsupported Clickhouse type: " + name);
	}
}

bool ClickhouseTypes::IsNullable(const ClickhouseTypeData &input) {
	return StringUtil::Contains(input.type, "Nullable");
}

} // namespace duckdb
