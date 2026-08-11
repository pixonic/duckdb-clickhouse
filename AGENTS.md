
# Agent Guidelines for DuckDB ClickHouse Extension

This document provides comprehensive guidelines for AI coding agents working on the DuckDB ClickHouse Scanner extension.

## Project Overview

This is a DuckDB extension that enables direct reading from ClickHouse databases. The extension is built using C++17 and follows DuckDB's extension architecture patterns.

**Current Status**: Work-in-Progress (WIP) - Not recommended for production use.

## Build Commands

### Prerequisites
- Initialize submodules: `git submodule update --init --recursive`
- Install vcpkg: See README.md for vcpkg setup instructions
- Set `VCPKG_TOOLCHAIN_PATH` environment variable

### Primary Build Targets
```bash
make                    # Build release version (default)
make release            # Build release version
make debug              # Build debug version
make reldebug           # Build release with debug info
make relassert          # Build release with assertions
```

### Testing Commands
```bash
make test               # Run all tests (release)
make test_release       # Run tests with release build
make test_debug         # Run tests with debug build
```

### Run Single Test
```bash
# Run specific test file
./build/release/test/unittest "test/sql/clickhouse_catalog.test"

# Run tests matching pattern
./build/release/test/unittest "test/*pattern*"
```

### Lint/Format Commands
```bash
make tidy-check-fixed   # Run clang-tidy on extension source code
```

The tidy check runs clang-tidy on files matching `src/.*` with the project's `.clang-tidy` configuration.
Don

## Project Structure

```
src/
├── include/
│   ├── storage/               # Storage layer headers
│   ├── clickhouse_client.hpp  # ClickHouse client wrapper
│   ├── clickhouse_types.hpp   # Type conversion utilities
│   ├── clickhouse_utils.hpp   # Helper functions
│   └── clickhouse_scan.hpp    # Scan operation definitions
├── storage/                   # Catalog and transaction implementations
│   ├── clickhouse_catalog.cpp
│   ├── clickhouse_schema_*.cpp
│   ├── clickhouse_table_*.cpp
│   └── clickhouse_transaction*.cpp
├── clickhouse_client.cpp      # Client implementation
├── clickhouse_types.cpp       # Type mapping
├── clickhouse_scan.cpp        # Scan execution
└── clickhouse_scanner_extension.cpp  # Extension entry point

test/
└── sql/                       # SQLLogicTest files
    └── *.test                 # Test cases

third_party/
└── msd/                       # Channel library for async operations
```

## Code Style Guidelines

### Formatting (Enforced by .clang-format)

**Indentation & Spacing**:
- Use **TABS** for indentation (TabWidth: 4, IndentWidth: 4)
- Column limit: 120 characters
- Pointer alignment: RIGHT (`Type *ptr`, not `Type* ptr`)
- Space before C++11 braced lists, constructor colons, inheritance colons

**Functions & Control Flow**:
- No short functions on single line (`AllowShortFunctionsOnASingleLine: false`)
- No short loops on single line
- Lambdas can be inline
- Always break template declarations on new lines
- Braces required around statements (readability-braces-around-statements)

**Includes**:
- `SortIncludes: false` - Maintain manual include order
- Do NOT sort includes alphabetically
- Typical order: Local headers first, then DuckDB headers, then third-party, then standard library

### Naming Conventions (Enforced by .clang-tidy)

- **Classes/Enums**: `CamelCase` (e.g., `ClickhouseCatalog`, `ClickhouseClient`)
- **Functions/Methods**: `CamelCase` (e.g., `GetEntry`, `ScanSchemas`)
- **Variables/Parameters**: `lower_case` (e.g., `client_options`, `schema_name`)
- **Member Variables**: `lower_case` (e.g., `attach_path`, `channel_size`)
- **Typedefs**: `lower_case` with `_t` suffix (e.g., `my_type_t`)
- **Constants/Static**: `UPPER_CASE` (e.g., `DEFAULT_SCHEMA`)
- **Macros**: `UPPER_CASE` (e.g., `DUCKDB_EXTENSION`)
- **Namespaces**: `lower_case` (e.g., `duckdb`)

### C++ Standards & Best Practices

**Language Version**: C++17

**Type Usage**:
- Use `nullptr` instead of `NULL` (modernize-use-nullptr)
- Use `override` keyword for virtual functions (modernize-use-override)
- Use `bool` literals (`true`/`false`) not integers (modernize-use-bool-literals)
- Use `emplace` over `push_back` where appropriate (modernize-use-emplace)
- Avoid C-style casts, prefer C++ casts (cppcoreguidelines-pro-type-cstyle-cast)

**Constructors**:
- Mark single-argument constructors as `explicit` (google-explicit-constructor)
- Never use `using namespace` in headers (google-build-using-namespace)

**Error Handling**:
- Use exception base classes properly (hicpp-exception-baseclass)
- Throw by value, catch by reference (misc-throw-by-value-catch-by-reference)
- Use DuckDB exception types: `NotImplementedException`, `InvalidInputException`, `BinderException`

**Resource Management**:
- Prefer DuckDB smart pointers: `duckdb::unique_ptr`, `duckdb::shared_ptr`
- Use `std::optional` for optional values (not raw pointers)
- Ensure virtual destructors for base classes (cppcoreguidelines-virtual-class-destructor)

**Code Organization**:
- Avoid global variables (cppcoreguidelines-avoid-non-const-global-variables)
- Use header guards (llvm-header-guard)
- Keep definitions out of headers (misc-definitions-in-headers)

### File Formatting

**Line Endings**: Unix-style (`\n`)
- Always end files with newline (`insert_final_newline: true`)
- Trim trailing whitespace in code files
- Encoding: UTF-8

### DuckDB-Specific Patterns

**Namespace**: All code in `namespace duckdb { ... }`

**Catalog Integration**:
- Extend DuckDB base classes: `Catalog`, `SchemaCatalogEntry`, `TableCatalogEntry`
- Use `CatalogTransaction` for transactional operations
- Return `optional_ptr<T>` for nullable catalog entries

**Type Conversion**:
- Map ClickHouse types to DuckDB `LogicalType`
- Handle nullable types explicitly

**Error Messages**:
- Provide clear, actionable error messages
- Include relevant context (table names, schema names, etc.)

## Testing Guidelines

**Test File Format**: SQLLogicTests (`.test` files)

**Test Structure**:
```sql
# name: test/sql/test_name.test
# description: Test description
# group: [sql]

require clickhouse_scanner

statement ok
ATTACH 'host=localhost port=9000 database=test_db' AS ch (TYPE clickhouse_scanner);

query I
SELECT * FROM ch.schema.table;
----
expected_result
```

**Test Requirements**:
- Most tests require a running ClickHouse server
- Initialize test database: `clickhouse-client < scripts/setup_clickhouse.sql`
- Tests use connection string: `host=localhost port=9000 database=test_db`

## Common Patterns

### Catalog Entry Retrieval
```cpp
auto &ch_transaction = ClickhouseTransaction::Get(context, *this);
auto entry = schemas.GetEntry(ch_transaction, name);
if (!entry && if_not_found != OnEntryNotFound::RETURN_NULL) {
    throw BinderException("Entry \"%s\" not found", name);
}
```

### Async Query Execution
```cpp
auto channel = std::make_shared<BlockChannel>(channel_size);
std::thread t(&ClickhouseClient::ExecQuery, this, sql, channel);
t.detach();
return ClickhouseResult(channel);
```

## Warnings & Constraints

- **Thread Safety**: Use `std::mutex` for client operations
- **Async Operations**: Channel-based communication for non-blocking queries
- **Read-Only**: Write operations throw `NotImplementedException`
- **Type Mapping**: Not all ClickHouse types supported yet
- **Clang-Tidy**: Warnings treated as errors (`WarningsAsErrors: '*'`)

## Development Workflow

1. Make code changes
2. Run `GEN=ninja make` to build
3. Run `make test` to verify tests pass
4. Run `make tidy-check-fixed` to check code style
5. Fix any clang-tidy warnings/errors
6. Commit changes with clear messages

## Additional Resources

- Test syntax: [DuckDB SQLLogicTest Documentation](https://duckdb.org/dev/sqllogictest/intro.html)
- Extension template: `duckdb/extension/` directory in DuckDB repository
