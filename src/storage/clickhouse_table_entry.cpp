#include "storage/clickhouse_table_entry.hpp"
#include "clickhouse_scan.hpp"

namespace duckdb {

ClickhouseTableEntry::ClickhouseTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, CreateTableInfo &info)
    : TableCatalogEntry(catalog, schema, info) {
}

unique_ptr<BaseStatistics> ClickhouseTableEntry::GetStatistics(ClientContext &context, column_t column_id) {
	throw NotImplementedException("GetStatistics");
}

TableFunction ClickhouseTableEntry::GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) {
	auto scan_bind_data = make_uniq<ClickhouseScanBindData>(*this);
	
	for (auto &col : GetColumns().Logical()) {
		scan_bind_data->column_names.push_back(col.GetName());
		scan_bind_data->column_types.push_back(col.GetType());
	}
	bind_data = std::move(scan_bind_data);
	
	return ClickhouseScanFunction();
}

TableStorageInfo ClickhouseTableEntry::GetStorageInfo(ClientContext &context) {
	return TableStorageInfo();
	// throw NotImplementedException("GetStorageInfo");
}

void ClickhouseTableEntry::BindUpdateConstraints(Binder &binder, LogicalGet &get, LogicalProjection &proj,
                                                 LogicalUpdate &update, ClientContext &context) {
}

} // namespace duckdb
