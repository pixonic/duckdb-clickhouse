#include "clickhouse_types.hpp"

namespace duckdb {

static bool TryUnwrap(const string &type, const string &wrapper, string &nested_type) {
	auto prefix = wrapper + "(";
	if (!StringUtil::StartsWith(type, prefix) || !StringUtil::EndsWith(type, ")")) {
		return false;
	}
	nested_type = type.substr(prefix.size(), type.size() - prefix.size() - 1);
	return true;
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

static LogicalType DateTime64ToLogicalType(const ClickhouseTypeData &input, const string &type,
                                           bool use_metadata_precision) {
	auto declaration_precision = ParseTemporalPrecision(type, "DateTime64");
	auto precision = use_metadata_precision ? input.datetime_precision : -1;
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

static std::optional<LogicalType> TypeToLogicalType(const ClickhouseTypeData &input, const string &type,
                                                    bool use_metadata_precision) {
	string nested_type;
	if (TryUnwrap(type, "Nullable", nested_type)) {
		return TypeToLogicalType(input, nested_type, use_metadata_precision);
	}
	if (TryUnwrap(type, "LowCardinality", nested_type)) {
		auto logical_type_opt = TypeToLogicalType(input, nested_type, use_metadata_precision);
		if (!logical_type_opt || logical_type_opt.value().id() != LogicalTypeId::VARCHAR) {
			return std::nullopt;
		}
		return std::optional(logical_type_opt.value());
	}
	if (TryUnwrap(type, "Array", nested_type)) {
		auto child_type = TypeToLogicalType(input, nested_type, false);
		if (child_type) {
			return std::optional(LogicalType::LIST(child_type.value()));
		}
		return std::nullopt;
	}

	if (type == "Date32") {
		return std::optional(LogicalType::DATE);
	} else if (StringUtil::StartsWith(type, "DateTime64(")) {
		return std::optional(DateTime64ToLogicalType(input, type, use_metadata_precision));
	} else if (IsParameterizedType(type, "DateTime")) {
		return std::optional(LogicalType::TIMESTAMP);
	} else if (type == "Date") {
		return std::optional(LogicalType::DATE);
	} else if (type == "Time") {
		return std::optional(LogicalType::TIME);
	} else if (StringUtil::StartsWith(type, "Time64(")) {
		auto precision = ParseTemporalPrecision(type, "Time64");
		auto t = precision <= 6 ? LogicalType::TIME : LogicalType::TIME_NS;
		return std::optional(t);
	} else if (type == "Bool") {
		return std::optional(LogicalType::BOOLEAN);
	} else if (type == "UInt8") {
		return std::optional(LogicalType::UTINYINT);
	} else if (type == "Int8") {
		return std::optional(LogicalType::TINYINT);
	} else if (type == "UInt16") {
		return std::optional(LogicalType::USMALLINT);
	} else if (type == "Int16") {
		return std::optional(LogicalType::SMALLINT);
	} else if (type == "UInt32") {
		return std::optional(LogicalType::UINTEGER);
	} else if (type == "Int32") {
		return std::optional(LogicalType::INTEGER);
	} else if (type == "UInt64") {
		return std::optional(LogicalType::UBIGINT);
	} else if (type == "Int64") {
		return std::optional(LogicalType::BIGINT);
	} else if (type == "Float32") {
		return std::optional(LogicalType::FLOAT);
	} else if (type == "Float64") {
		return std::optional(LogicalType::DOUBLE);
	} else if (type == "UUID") {
		return std::optional(LogicalType::UUID);
	} else if (type == "String" || IsParameterizedType(type, "FixedString")) {
		return std::optional(LogicalType::VARCHAR);
	} else {
		return std::nullopt;
	}
}

std::optional<LogicalType> ClickhouseTypes::TypeToLogicalType(const ClickhouseTypeData &input) {
	return duckdb::TypeToLogicalType(input, input.type, true);
}

bool ClickhouseTypes::IsNullable(const ClickhouseTypeData &input) {
	string nested_type;
	if (TryUnwrap(input.type, "Nullable", nested_type)) {
		return true;
	}
	return TryUnwrap(input.type, "LowCardinality", nested_type) && TryUnwrap(nested_type, "Nullable", nested_type);
}

} // namespace duckdb
