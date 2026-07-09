#pragma once

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/common/enums/access_mode.hpp"

#include "storage/clickhouse_schema_set.hpp"

#include <clickhouse/client.h>

namespace duckdb {

class ClickhouseCatalog : public Catalog {
public:
	explicit ClickhouseCatalog(AttachedDatabase &db, string attach_path, AccessMode access_mode,
	                           clickhouse::ClientOptions client_options);

	~ClickhouseCatalog() override;

	string attach_path;
	AccessMode access_mode;
	clickhouse::ClientOptions client_options;

public:
	void Initialize(bool load_builtin) override;

	string GetCatalogType() override {
		return "clickhouse";
	}

	optional_ptr<CatalogEntry> CreateSchema(CatalogTransaction transaction, CreateSchemaInfo &info) override;

	void ScanSchemas(ClientContext &context, std::function<void(SchemaCatalogEntry &)> callback) override;

	optional_ptr<SchemaCatalogEntry> LookupSchema(CatalogTransaction transaction, const EntryLookupInfo &schema_lookup,
	                                              OnEntryNotFound if_not_found) override;

	PhysicalOperator &PlanCreateTableAs(ClientContext &context, PhysicalPlanGenerator &planner, LogicalCreateTable &op,
	                                    PhysicalOperator &plan) override;

	PhysicalOperator &PlanInsert(ClientContext &context, PhysicalPlanGenerator &planner, LogicalInsert &op,
	                             optional_ptr<PhysicalOperator> plan) override;

	PhysicalOperator &PlanDelete(ClientContext &context, PhysicalPlanGenerator &planner, LogicalDelete &op,
	                             PhysicalOperator &plan) override;

	PhysicalOperator &PlanUpdate(ClientContext &context, PhysicalPlanGenerator &planner, LogicalUpdate &op,
	                             PhysicalOperator &plan) override;

	DatabaseSize GetDatabaseSize(ClientContext &context) override;

	bool InMemory() override;

	string GetDBPath() override;

private:
	void DropSchema(ClientContext &context, DropInfo &info) override;

private:
	ClickhouseSchemaSet schemas;
};

} // namespace duckdb
