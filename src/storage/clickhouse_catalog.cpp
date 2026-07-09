
#include "duckdb/storage/database_size.hpp"
#include "duckdb/common/exception/binder_exception.hpp"

#include "storage/clickhouse_schema_entry.hpp"
#include "storage/clickhouse_catalog.hpp"

namespace duckdb {

ClickhouseCatalog::ClickhouseCatalog(AttachedDatabase &db, string attach_path, AccessMode access_mode,
                                     clickhouse::ClientOptions client_options)
    : Catalog(db), attach_path(std::move(attach_path)), access_mode(access_mode),
      client_options(std::move(client_options)), schemas(*this) {
}

ClickhouseCatalog::~ClickhouseCatalog() = default;

void ClickhouseCatalog::Initialize(bool load_builtin) {
}

optional_ptr<CatalogEntry> ClickhouseCatalog::CreateSchema(CatalogTransaction transaction, CreateSchemaInfo &info) {
	throw NotImplementedException("CreateSchema");
}

void ClickhouseCatalog::ScanSchemas(ClientContext &context, std::function<void(SchemaCatalogEntry &)> callback) {
	auto &ch_transaction = ClickhouseTransaction::Get(context, *this);

	schemas.Scan(ch_transaction, [&](CatalogEntry &schema) { callback(schema.Cast<ClickhouseSchemaEntry>()); });
}

optional_ptr<SchemaCatalogEntry> ClickhouseCatalog::LookupSchema(CatalogTransaction transaction,
                                                                 const EntryLookupInfo &schema_lookup,
                                                                 OnEntryNotFound if_not_found) {

	auto schema_name = schema_lookup.GetEntryName();
	if (schema_name == DEFAULT_SCHEMA) {
		std::string &default_schema = client_options.default_database;
		if (default_schema.empty()) {
			throw InvalidInputException("Attempting to fetch the default schema - but no database was "
			                            "provided in the connection string");
		}
		schema_name = default_schema;
	}

	auto &ch_transaction = ClickhouseTransaction::Get(transaction.GetContext(), *this);

	auto entry = schemas.GetEntry(ch_transaction, schema_name);
	if (!entry && if_not_found != OnEntryNotFound::RETURN_NULL) {
		throw BinderException("Schema with name \"%s\" not found", schema_name);
	}

	return reinterpret_cast<SchemaCatalogEntry *>(entry.get());
}

PhysicalOperator &ClickhouseCatalog::PlanCreateTableAs(ClientContext &context, PhysicalPlanGenerator &planner,
                                                       LogicalCreateTable &op, PhysicalOperator &plan) {
	throw NotImplementedException("PlanCreateTableAs");
}

PhysicalOperator &ClickhouseCatalog::PlanInsert(ClientContext &context, PhysicalPlanGenerator &planner,
                                                LogicalInsert &op, optional_ptr<PhysicalOperator> plan) {
	throw NotImplementedException("PlanInsert");
}

PhysicalOperator &ClickhouseCatalog::PlanDelete(ClientContext &context, PhysicalPlanGenerator &planner,
                                                LogicalDelete &op, PhysicalOperator &plan) {
	throw NotImplementedException("PlanDelete");
}

PhysicalOperator &ClickhouseCatalog::PlanUpdate(ClientContext &context, PhysicalPlanGenerator &planner,
                                                LogicalUpdate &op, PhysicalOperator &plan) {
	throw NotImplementedException("PlanUpdate");
}

DatabaseSize ClickhouseCatalog::GetDatabaseSize(ClientContext &context) {
	throw NotImplementedException("GetDatabaseSize");
}

bool ClickhouseCatalog::InMemory() {
	return false;
}

string ClickhouseCatalog::GetDBPath() {
	return attach_path;
}

void ClickhouseCatalog::DropSchema(ClientContext &context, DropInfo &info) {
	throw NotImplementedException("DropSchema");
}

} // namespace duckdb
