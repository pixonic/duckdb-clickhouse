#pragma once

#include "storage/clickhouse_transaction.hpp"
#include "storage/clickhouse_catalog_set.hpp"

namespace duckdb {

class ClickhouseSchemaSet : public ClickhouseCatalogSet {
public:
	explicit ClickhouseSchemaSet(Catalog &catalog);

protected:
	void LoadEntries(ClickhouseTransaction &transaction) override;
};

} // namespace duckdb
