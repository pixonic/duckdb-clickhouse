#pragma once

#include "storage/clickhouse_transaction.hpp"

namespace duckdb {

class ClickhouseCatalogSet {
public:
	explicit ClickhouseCatalogSet(Catalog &catalog);
	virtual ~ClickhouseCatalogSet() = default;

	void Scan(ClickhouseTransaction &transaction, const std::function<void(CatalogEntry &)> &callback);
	optional_ptr<CatalogEntry> GetEntry(ClickhouseTransaction &transaction, const string &name);
	optional_ptr<CatalogEntry> CreateEntry(unique_ptr<CatalogEntry> entry);

protected:
	virtual void LoadEntries(ClickhouseTransaction &transaction) = 0;
	void TryLoadEntries(ClickhouseTransaction &transaction);

protected:
	Catalog &catalog;

private:
	mutex entry_lock;
	unordered_map<string, shared_ptr<CatalogEntry>> entries;
	case_insensitive_map_t<string> entry_map;
	atomic<bool> is_loaded;
};

} // namespace duckdb
