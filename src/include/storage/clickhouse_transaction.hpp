#pragma once

#include <mutex>

#include "duckdb/transaction/transaction.hpp"

#include "clickhouse_client.hpp"

namespace duckdb {

class ClickhouseTransaction : public Transaction {
public:
	ClickhouseTransaction(Catalog &catalog, TransactionManager &manager, ClientContext &context);
	~ClickhouseTransaction() override;

	unique_ptr<ClickhouseClient> NewClient();

	static ClickhouseTransaction &Get(ClientContext &context, Catalog &catalog);

private:
	clickhouse::ClientOptions client_options;
};

} // namespace duckdb
