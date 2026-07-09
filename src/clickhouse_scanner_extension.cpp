#define DUCKDB_EXTENSION_MAIN

#include "clickhouse_scanner_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"

#include "storage/clickhouse_storage_extension.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
	auto &db = loader.GetDatabaseInstance();
	auto &config = DBConfig::GetConfig(db);

	StorageExtension::Register(config, "clickhouse_scanner", make_shared_ptr<ClickhouseStorageExtension>());
}

void ClickhouseScannerExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}
std::string ClickhouseScannerExtension::Name() {
	return "clickhouse_scanner";
}

std::string ClickhouseScannerExtension::Version() const {
#ifdef EXT_VERSION_CLICKHOUSE_SCANNER
	return EXT_VERSION_CLICKHOUSE_SCANNER;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(clickhouse_scanner, loader) {
	duckdb::LoadInternal(loader);
}
}
