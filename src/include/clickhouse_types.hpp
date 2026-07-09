#pragma once

#include "duckdb/common/types.hpp"
#include "duckdb/common/string_util.hpp"

namespace duckdb {

struct ClickhouseTypeData {
	string type;
	int64_t precision;
	int64_t scale;

	ClickhouseTypeData(string type, int64_t precision, int64_t scale)
	    : type(std::move(type)), precision(precision), scale(scale) {
	}
};

class ClickhouseTypes {
public:
	static LogicalType TypeToLogicalType(const ClickhouseTypeData &input);
	static bool IsNullable(const ClickhouseTypeData &input);
};

} // namespace duckdb
