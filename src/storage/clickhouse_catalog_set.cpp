#include "storage/clickhouse_catalog_set.hpp"

namespace duckdb {

ClickhouseCatalogSet::ClickhouseCatalogSet(Catalog &catalog) : catalog(catalog), is_loaded(false) {
}

void ClickhouseCatalogSet::Scan(ClickhouseTransaction &transaction,
                                const std::function<void(CatalogEntry &)> &callback) {
	TryLoadEntries(transaction);
	lock_guard<mutex> l(entry_lock);
	for (auto &entry : entries) {
		callback(*entry.second);
	}
}

optional_ptr<CatalogEntry> ClickhouseCatalogSet::GetEntry(ClickhouseTransaction &transaction, const string &name) {
	TryLoadEntries(transaction);

	lock_guard<mutex> l(entry_lock);

	auto entry = entries.find(name);
	if (entry == entries.end()) {
		return nullptr;
	}
	return entry->second.get();
}

void ClickhouseCatalogSet::TryLoadEntries(ClickhouseTransaction &transaction) {
	if (is_loaded) {
		return;
	}
	LoadEntries(transaction);
	is_loaded = true;
}

optional_ptr<CatalogEntry> ClickhouseCatalogSet::CreateEntry(unique_ptr<CatalogEntry> entry) {
	lock_guard<mutex> l(entry_lock);
	auto result = entry.get();
	if (result->name.empty()) {
		throw InternalException("MySQLCatalogSet::CreateEntry called with empty name");
	}
	entries.insert(make_pair(result->name, std::move(entry)));
	return result;
}

} // namespace duckdb
