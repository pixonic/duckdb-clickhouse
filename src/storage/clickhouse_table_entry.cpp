#include "storage/clickhouse_table_entry.hpp"
#include "clickhouse_scan.hpp"

namespace duckdb {

ClickhouseTableEntry::ClickhouseTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, CreateTableInfo &info,
                                           vector<ClickhouseColumnDefinition> columns)
    : TableCatalogEntry(catalog, schema, info), ch_columns(std::move(columns)) {
}

unique_ptr<BaseStatistics> ClickhouseTableEntry::GetStatistics(ClientContext &context, column_t column_id) {
	throw NotImplementedException("GetStatistics");
}

TableFunction ClickhouseTableEntry::GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) {
	auto scan_bind_data = make_uniq<ClickhouseScanBindData>(*this);

	bind_data = std::move(scan_bind_data);

	return ClickhouseScanFunction();
}

TableStorageInfo ClickhouseTableEntry::GetStorageInfo(ClientContext &context) {
	return TableStorageInfo();
}

void ClickhouseTableEntry::BindUpdateConstraints(Binder &binder, LogicalGet &get, LogicalProjection &proj,
                                                 LogicalUpdate &update, ClientContext &context) {
}

const ClickhouseColumnDefinition &ClickhouseTableEntry::GetClickhouseColumn(column_t index) const {
	if (index >= ch_columns.size()) {
		throw InternalException("Clickhouse column index %lld out of range", index);
	}
	return ch_columns[index];
}

} // namespace duckdb
