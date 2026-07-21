#include "clickhouse_types.hpp"

namespace duckdb {

static string UnwrapNullable(const string &type) {
	const string nullable_prefix = "Nullable(";
	if (StringUtil::StartsWith(type, nullable_prefix) && StringUtil::EndsWith(type, ")")) {
		return type.substr(nullable_prefix.size(), type.size() - nullable_prefix.size() - 1);
	}
	return type;
}

static bool IsParameterizedType(const string &type, const string &name) {
	return type == name || (StringUtil::StartsWith(type, name + "(") && StringUtil::EndsWith(type, ")"));
}

static int64_t ParseTemporalPrecision(const string &type, const string &name) {
	auto prefix = name + "(";
	if (!StringUtil::StartsWith(type, prefix) || !StringUtil::EndsWith(type, ")")) {
		throw InternalException("Invalid ClickHouse %s declaration: %s", name, type);
	}

	auto precision_start = prefix.size();
	auto precision_end = type.find(',', precision_start);
	if (precision_end == string::npos) {
		precision_end = type.size() - 1;
	}
	if (precision_start == precision_end) {
		throw InternalException("Invalid ClickHouse %s declaration: %s", name, type);
	}

	int64_t precision = 0;
	for (auto i = precision_start; i < precision_end; i++) {
		if (type[i] < '0' || type[i] > '9') {
			throw InternalException("Invalid ClickHouse %s precision in declaration: %s", name, type);
		}
		precision = precision * 10 + type[i] - '0';
	}
	if (precision < 0 || precision > 9) {
		throw InternalException("Unsupported ClickHouse %s precision %d", name, precision);
	}
	return precision;
}

static LogicalType DateTime64ToLogicalType(const ClickhouseTypeData &input, const string &type) {
	auto declaration_precision = ParseTemporalPrecision(type, "DateTime64");
	auto precision = input.datetime_precision;
	if (precision < 0) {
		precision = declaration_precision;
	} else if (precision != declaration_precision) {
		throw InternalException("ClickHouse DateTime64 precision metadata does not match declaration: %s", input.type);
	}

	if (precision == 0) {
		return LogicalType::TIMESTAMP_S;
	} else if (precision <= 3) {
		return LogicalType::TIMESTAMP_MS;
	} else if (precision <= 6) {
		return LogicalType::TIMESTAMP;
	}
	return LogicalType::TIMESTAMP_NS;
}

LogicalType ClickhouseTypes::TypeToLogicalType(const ClickhouseTypeData &input) {
	auto name = UnwrapNullable(input.type);

	// TODO support other types

	if (name == "Date32") {
		return LogicalType::DATE;
	} else if (StringUtil::StartsWith(name, "DateTime64(")) {
		return DateTime64ToLogicalType(input, name);
	} else if (IsParameterizedType(name, "DateTime")) {
		return LogicalType::TIMESTAMP;
	} else if (name == "Date") {
		return LogicalType::DATE;
	} else if (name == "Time") {
		return LogicalType::TIME;
	} else if (StringUtil::StartsWith(name, "Time64(")) {
		auto precision = ParseTemporalPrecision(name, "Time64");
		return precision <= 6 ? LogicalType::TIME : LogicalType::TIME_NS;
	} else if (StringUtil::Contains(name, "Bool")) {
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
	} else {
		throw InternalException("Unsupported Clickhouse type: " + name);
	}
}

bool ClickhouseTypes::IsNullable(const ClickhouseTypeData &input) {
	return StringUtil::Contains(input.type, "Nullable");
}

} // namespace duckdb
