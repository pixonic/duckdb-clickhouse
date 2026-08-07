#include "duckdb.hpp"

#include "duckdb/common/string_util.hpp"

#include "storage/clickhouse_storage_extension.hpp"
#include "storage/clickhouse_catalog.hpp"
#include "storage/clickhouse_transaction_manager.hpp"

#include "clickhouse_utils.hpp"

#include <clickhouse/client.h>

namespace duckdb {

static unique_ptr<Catalog> ClickhouseAttach(optional_ptr<StorageExtensionInfo> storage_info, ClientContext &context,
                                            AttachedDatabase &db, const string &name, AttachInfo &info,
                                            AttachOptions &attach_options) {
	auto &config = DBConfig::GetConfig(context);
	string attach_path = info.path;
	auto client_options = ClickhouseUtils::ParseOptions(attach_path);
	return make_uniq<ClickhouseCatalog>(db, std::move(attach_path), attach_options.access_mode,
	                                    std::move(client_options));
}

static unique_ptr<TransactionManager>
ClickhouseCreateTransactionManager(optional_ptr<StorageExtensionInfo> storage_info, AttachedDatabase &db,
                                   Catalog &catalog) {
	return make_uniq<ClickhouseTransactionManager>(db, catalog);
}

ClickhouseStorageExtension::ClickhouseStorageExtension() {
	attach = ClickhouseAttach;
	create_transaction_manager = ClickhouseCreateTransactionManager;
};

} // namespace duckdb
