#include "storage/clickhouse_transaction_manager.hpp"
#include "storage/clickhouse_transaction.hpp"

namespace duckdb {

ClickhouseTransactionManager::ClickhouseTransactionManager(AttachedDatabase &db, Catalog &catalog)
    : TransactionManager(db), catalog(catalog) {
}

Transaction &ClickhouseTransactionManager::StartTransaction(ClientContext &context) {
	auto transaction = make_uniq<ClickhouseTransaction>(catalog, *this, context);
	auto &result = *transaction;
	lock_guard<mutex> l(transaction_lock);
	transactions[result] = std::move(transaction);
	return result;
}

ErrorData ClickhouseTransactionManager::CommitTransaction(ClientContext &context, Transaction &transaction) {
	auto &ch_transaction = transaction.Cast<ClickhouseTransaction>();
	lock_guard<mutex> l(transaction_lock);
	transactions.erase(transaction);
	return ErrorData();
}

void ClickhouseTransactionManager::RollbackTransaction(Transaction &transaction) {
	auto &ch_transaction = transaction.Cast<ClickhouseTransaction>();
	lock_guard<mutex> l(transaction_lock);
	transactions.erase(transaction);
}

void ClickhouseTransactionManager::Checkpoint(ClientContext &context, bool force) {
	// no-op
}

} // namespace duckdb
