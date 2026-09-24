#include <thread>

#include "duckdb/common/exception.hpp"

#include "storage/clickhouse_transaction.hpp"
#include "storage/clickhouse_catalog.hpp"

namespace duckdb {

ClickhouseTransaction::ClickhouseTransaction(Catalog &catalog, TransactionManager &manager, ClientContext &context)
    : Transaction(manager, context), client_options(catalog.Cast<ClickhouseCatalog>().client_options) {
}

ClickhouseTransaction::~ClickhouseTransaction() = default;

unique_ptr<ClickhouseClient> ClickhouseTransaction::NewClient() {
	try {
		return make_uniq<ClickhouseClient>(client_options, 10);
	} catch (const std::exception &error) {
		throw IOException(error.what());
	}
}

ClickhouseTransaction &ClickhouseTransaction::Get(ClientContext &context, Catalog &catalog) {
	return Transaction::Get(context, catalog).Cast<ClickhouseTransaction>();
}

} // namespace duckdb
