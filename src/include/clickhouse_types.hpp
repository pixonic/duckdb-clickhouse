#pragma once

#include <optional>

#include "duckdb/common/types.hpp"
#include "duckdb/common/string_util.hpp"

namespace duckdb {

struct ClickhouseTypeData {
	string type;
	int64_t precision;
	int64_t scale;
	int64_t datetime_precision;

	ClickhouseTypeData(string type, int64_t precision, int64_t scale, int64_t datetime_precision)
	    : type(std::move(type)), precision(precision), scale(scale), datetime_precision(datetime_precision) {
	}
};

class ClickhouseTypes {
public:
	static std::optional<LogicalType> TypeToLogicalType(const ClickhouseTypeData &input);
	static bool IsNullable(const ClickhouseTypeData &input);
};

} // namespace duckdb
