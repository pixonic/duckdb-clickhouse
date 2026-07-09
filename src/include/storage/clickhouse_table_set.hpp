#pragma once

#include "storage/clickhouse_transaction.hpp"
#include "storage/clickhouse_catalog_set.hpp"

#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"

namespace duckdb {

class ClickhouseTableSet : public ClickhouseCatalogSet {
public:
	ClickhouseTableSet(SchemaCatalogEntry &schema, Catalog &catalog);

protected:
	void LoadEntries(ClickhouseTransaction &transaction) override;

private:
	SchemaCatalogEntry &schema;
};

} // namespace duckdb
