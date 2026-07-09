cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_STANDARD 17)
set(TARGET_NAME clickhouse_scanner)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})


# clickhouse-cpp dependencies
find_path(CLICKHOUSE_CPP_INCLUDE_DIR clickhouse/client.h)
find_library(CLICKHOUSE_CPP_LIB clickhouse-cpp-lib)
find_library(CITYHASH_LIB cityhash)
find_library(ZSTD_LIB zstd)
find_library(LZ4_LIB lz4)

include_directories(${CLICKHOUSE_CPP_INCLUDE_DIR})
include_directories(third_party)

add_subdirectory(src)

build_static_extension(${TARGET_NAME} ${ALL_OBJECT_FILES})
build_loadable_extension(${TARGET_NAME} " " ${ALL_OBJECT_FILES})

target_link_libraries(${EXTENSION_NAME} ${CLICKHOUSE_CPP_LIB} ${CITYHASH_LIB} ${ZSTD_LIB} ${LZ4_LIB})
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${CLICKHOUSE_CPP_LIB} ${CITYHASH_LIB} ${ZSTD_LIB} ${LZ4_LIB})

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
