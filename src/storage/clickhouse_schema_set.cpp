#include "storage/clickhouse_schema_set.hpp"
#include "storage/clickhouse_schema_entry.hpp"

#include "duckdb/parser/parsed_data/create_schema_info.hpp"

namespace duckdb {

static bool IsSchemaInternal(const string &name) {
	if (name == "information_schema" || name == "INFORMATION_SCHEMA" || name == "system") {
		return true;
	}
	return false;
}

ClickhouseSchemaSet::ClickhouseSchemaSet(Catalog &catalog) : ClickhouseCatalogSet(catalog) {
}

void ClickhouseSchemaSet::LoadEntries(ClickhouseTransaction &transaction) {
	auto result = transaction.GetClient().Query("SELECT name FROM system.databases");
	while (true) {
		auto block_opt = result.Next();
		if (!block_opt.has_value()) {
			break;
		}
		const auto &block = block_opt.value();

		for (auto i = 0; i < block.GetRowCount(); i++) {
			std::string_view view_name = block[0]->As<clickhouse::ColumnString>()->At(i);
			std::string name = std::string(view_name);

			if (IsSchemaInternal(name)) {
				// TODO implement
				continue;
			}

			CreateSchemaInfo info;
			// info.internal = IsSchemaInternal(name);
			info.internal = false;
			info.schema = std::move(name);

			auto schema = make_uniq<ClickhouseSchemaEntry>(catalog, info);
			CreateEntry(std::move(schema));
		}
	}
}

} // namespace duckdb
