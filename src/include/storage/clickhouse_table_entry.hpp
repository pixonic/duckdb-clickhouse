#pragma once

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/parser/parsed_data/create_table_info.hpp"
#include "duckdb/storage/table_storage_info.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

class ClickhouseTableEntry : public TableCatalogEntry {
public:
	ClickhouseTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, CreateTableInfo &info,
	                     vector<string> source_column_types);

	vector<string> source_column_types;

	unique_ptr<BaseStatistics> GetStatistics(ClientContext &context, column_t column_id) override;

	TableFunction GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) override;

	TableStorageInfo GetStorageInfo(ClientContext &context) override;

	void BindUpdateConstraints(Binder &binder, LogicalGet &get, LogicalProjection &proj, LogicalUpdate &update,
	                           ClientContext &context) override;
};

} // namespace duckdb
