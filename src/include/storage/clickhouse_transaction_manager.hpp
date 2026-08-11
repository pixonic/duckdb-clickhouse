#pragma once

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/transaction/transaction_manager.hpp"
#include "duckdb/transaction/transaction.hpp"

namespace duckdb {

class ClickhouseTransactionManager : public TransactionManager {
public:
	ClickhouseTransactionManager(AttachedDatabase &db, Catalog &catalog);

	Transaction &StartTransaction(ClientContext &context) override;

	ErrorData CommitTransaction(ClientContext &context, Transaction &transaction) override;

	void RollbackTransaction(Transaction &transaction) override;

	void Checkpoint(ClientContext &context, bool force = false) override;

private:
	mutex transaction_lock;
	reference_map_t<Transaction, unique_ptr<Transaction>> transactions;
	Catalog &catalog;
};

} // namespace duckdb
