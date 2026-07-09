include_directories(include)

add_subdirectory(storage)

add_library(
  clickhouse_ext OBJECT
  clickhouse_utils.cpp clickhouse_types.cpp clickhouse_client.cpp
  clickhouse_scan.cpp clickhouse_scanner_extension.cpp
  clickhouse_conversion.cpp clickhouse_filter_pushdown.cpp)

set(ALL_OBJECT_FILES
    ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:clickhouse_ext>
    PARENT_SCOPE)
