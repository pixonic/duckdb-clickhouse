#pragma once

#include <mutex>

#include "duckdb/transaction/transaction.hpp"

#include "clickhouse_client.hpp"

namespace duckdb {

class ClickhouseTransaction : public Transaction {
public:
	ClickhouseTransaction(Catalog &catalog, TransactionManager &manager, ClientContext &context);
	~ClickhouseTransaction() override;

	ClickhouseClient &GetClient();

	static ClickhouseTransaction &Get(ClientContext &context, Catalog &catalog);

private:
	ClickhouseClient client;
};

} // namespace duckdb
