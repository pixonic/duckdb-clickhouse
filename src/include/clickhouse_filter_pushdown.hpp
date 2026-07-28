#pragma once

#include "duckdb/common/types.hpp"
#include "duckdb/planner/table_filter.hpp"
#include "duckdb/common/enums/expression_type.hpp"

namespace duckdb {

class ClickhouseFilterPushdown {
public:
	// Transform DuckDB table filters into ClickHouse WHERE clause
	static string TransformFilters(const vector<column_t> &column_ids, optional_ptr<TableFilterSet> filters,
	                               const vector<string> &names, const vector<string> &source_types);

private:
	static string TransformFilter(const string &column_name, const string &source_type, TableFilter &filter);
	static string CreateExpression(const string &column_name, const string &source_type,
	                               vector<unique_ptr<TableFilter>> &filters, const string &op);
	static string TransformComparison(ExpressionType type);
};

} // namespace duckdb
