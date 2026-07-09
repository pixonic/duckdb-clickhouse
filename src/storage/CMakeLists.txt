add_library(
  clickhouse_ext_storage OBJECT
  clickhouse_catalog_set.cpp
  clickhouse_catalog.cpp
  clickhouse_schema_entry.cpp
  clickhouse_schema_set.cpp
  clickhouse_table_set.cpp
  clickhouse_table_entry.cpp
  clickhouse_storage_extension.cpp
  clickhouse_transaction_manager.cpp
  clickhouse_transaction.cpp)

set(ALL_OBJECT_FILES
    ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:clickhouse_ext_storage>
    PARENT_SCOPE)
