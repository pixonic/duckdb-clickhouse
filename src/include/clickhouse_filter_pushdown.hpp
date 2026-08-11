#pragma once

#include "duckdb/common/types.hpp"
#include "duckdb/planner/table_filter.hpp"
#include "duckdb/common/enums/expression_type.hpp"
#include "storage/clickhouse_table_entry.hpp"

namespace duckdb {

class ClickhouseFilterPushdown {
public:
	// Transform DuckDB table filters into ClickHouse WHERE clause
	static string TransformFilters(const vector<column_t> &column_ids, optional_ptr<TableFilterSet> filters,
	                               const ClickhouseTableEntry &table);

private:
	static string TransformFilter(const string &column_name, TableFilter &filter);
	static string CreateExpression(const string &column_name, vector<unique_ptr<TableFilter>> &filters,
	                               const string &op);
};

} // namespace duckdb
