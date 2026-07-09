#pragma once

#include "duckdb/common/types.hpp"
#include "duckdb/planner/table_filter.hpp"
#include "duckdb/common/enums/expression_type.hpp"

namespace duckdb {

class ClickhouseFilterPushdown {
public:
	// Transform DuckDB table filters into ClickHouse WHERE clause
	static string TransformFilters(const vector<column_t> &column_ids, optional_ptr<TableFilterSet> filters,
	                               const vector<string> &names);

private:
	static string TransformFilter(string &column_name, TableFilter &filter);
	static string CreateExpression(string &column_name, vector<unique_ptr<TableFilter>> &filters, string op);
	static string TransformComparison(ExpressionType type);
};

} // namespace duckdb
