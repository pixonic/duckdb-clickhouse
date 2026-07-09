# ClickhouseScanFunction Implementation Plan

## Overview

This document describes the implementation of `ClickhouseScanFunction` - a table function that enables efficient scanning of ClickHouse tables with projection and filter pushdown capabilities, following the pattern established by DuckDB's `ArrowTableFunction`.

## Architecture

### Key Components

1. **`ClickhouseScanFunction`** (`src/clickhouse_scan.cpp`)
   - Main table function implementation
   - Handles bind, init (global/local), and scan operations
   - Supports multi-threaded scanning (up to 8 threads)
   - Implements projection and filter pushdown

2. **`ClickhouseConversion`** (`src/clickhouse_conversion.cpp`)
   - Zero-copy conversion from `clickhouse::Block` to `duckdb::Vector`
   - Supports numeric, string, date, and datetime types
   - Uses `ClickhouseAuxiliaryData` to manage Block lifetime

3. **`ClickhouseFilterPushdown`** (`src/clickhouse_filter_pushdown.cpp`)
   - Transforms DuckDB table filters to ClickHouse WHERE clauses
   - Adapted from duckdb-mysql filter pushdown implementation
   - Supports: IS NULL, comparisons, conjunctions, IN filters

4. **`ClickhouseAuxiliaryData`** (`src/include/clickhouse_conversion.hpp`)
   - Vector auxiliary data for managing ClickHouse Block lifetime
   - Enables zero-copy access to numeric data

## Data Flow

```
┌────────────────────────────────────────────────────────────────┐
│ User Query: SELECT * FROM ch.test_db.t1 WHERE id > 100        │
└────────────────────┬───────────────────────────────────────────┘
                     │
                     ▼
┌────────────────────────────────────────────────────────────────┐
│ ClickhouseScanFunction::Bind (via ClickhouseTableEntry)       │
│ - Create ClickhouseScanBindData                                │
│ - Extract column names/types from table schema                 │
└────────────────────┬───────────────────────────────────────────┘
                     │
                     ▼
┌────────────────────────────────────────────────────────────────┐
│ ClickhouseScanFunction::InitGlobal                             │
│ 1. Build SELECT with projection pushdown (only needed columns) │
│ 2. Apply filter pushdown (WHERE clause)                        │
│ 3. Execute query via ClickhouseClient                          │
│ 4. Store ClickhouseResult in global state                      │
│                                                                 │
│ Example SQL: SELECT id, name FROM test_db.t1 WHERE id > 100   │
└────────────────────┬───────────────────────────────────────────┘
                     │
                     ▼
┌────────────────────────────────────────────────────────────────┐
│ ClickhouseScanFunction::InitLocal (per thread)                 │
│ - Create local state                                            │
│ - Fetch first block from global result (thread-safe)           │
└────────────────────┬───────────────────────────────────────────┘
                     │
                     ▼
┌────────────────────────────────────────────────────────────────┐
│ ClickhouseScanFunction::Scan (called repeatedly)               │
│ 1. Check if current block exhausted                            │
│ 2. If exhausted, fetch next block (thread-safe)                │
│ 3. Convert Block → DataChunk (up to STANDARD_VECTOR_SIZE rows) │
│ 4. Increment block_offset                                      │
└────────────────────┬───────────────────────────────────────────┘
                     │
                     ▼
┌────────────────────────────────────────────────────────────────┐
│ ClickhouseConversion::BlockToDuckDB                            │
│ - For each column in output:                                   │
│   • Numeric: Zero-copy or fast memcpy                          │
│   • String: Copy via StringVector::AddString                   │
│   • Date/DateTime: Convert epoch formats                       │
│ - Handle NULL values (Nullable wrapper)                        │
└────────────────────────────────────────────────────────────────┘
```

## Implementation Details

### 1. Bind Phase (`ClickhouseScanFunction::Bind`)

**Location**: `src/clickhouse_scan.cpp:47`

**Purpose**: Initialize bind data with table metadata

**Flow**:
- Called via `ClickhouseTableEntry::GetScanFunction`
- Creates `ClickhouseScanBindData` containing:
  - Reference to table entry
  - Column names and types (extracted from table schema)
- **Note**: Direct Bind function throws exception - scan is integrated via catalog

**Integration**:
```cpp
// src/storage/clickhouse_table_entry.cpp:14
TableFunction ClickhouseTableEntry::GetScanFunction(...) {
    auto scan_bind_data = make_uniq<ClickhouseScanBindData>(*this);
    // Populate column metadata
    for (auto &col : GetColumns().Logical()) {
        scan_bind_data->column_names.push_back(col.GetName());
        scan_bind_data->column_types.push_back(col.GetType());
    }
    bind_data = std::move(scan_bind_data);
    return ClickhouseScanFunction();
}
```

### 2. Global Init (`ClickhouseScanFunction::InitGlobal`)

**Location**: `src/clickhouse_scan.cpp:57`

**Purpose**: Build and execute ClickHouse query with pushdowns

**Projection Pushdown**:
```cpp
// Only select requested columns
for (idx_t i = 0; i < input.column_ids.size(); i++) {
    auto col_idx = input.column_ids[i];
    if (col_idx == COLUMN_IDENTIFIER_ROW_ID) {
        select += "NULL";  // ROW_ID not in ClickHouse
    } else {
        auto &col = table.GetColumn(LogicalIndex(col_idx));
        select += ClickhouseUtils::WriteIdentifier(col.GetName());
    }
}
```

**Filter Pushdown**:
```cpp
string filter_string = ClickhouseFilterPushdown::TransformFilters(
    input.column_ids, input.filters, bind_data.column_names);
if (!filter_string.empty()) {
    select += " WHERE " + filter_string;
}
```

**Query Execution**:
```cpp
auto &transaction = ClickhouseTransaction::Get(context, table.catalog);
auto &client = transaction.GetClient();
auto result = make_uniq<ClickhouseResult>(client.Query(select));
```

### 3. Local Init (`ClickhouseScanFunction::InitLocal`)

**Location**: `src/clickhouse_scan.cpp:103`

**Purpose**: Initialize per-thread state

**Key Points**:
- Each thread gets own `ClickhouseScanLocalState`
- Fetches first block from global state (thread-safe via mutex)
- Returns `nullptr` if no blocks available

```cpp
auto local_state = make_uniq<ClickhouseScanLocalState>();
if (!GetNextBlock(context.client, *local_state, gstate)) {
    return nullptr;  // No data
}
return local_state;
```

### 4. Scan Function (`ClickhouseScanFunction::Scan`)

**Location**: `src/clickhouse_scan.cpp:158`

**Purpose**: Convert ClickHouse blocks to DuckDB chunks

**Main Loop**:
```cpp
// Check if current block exhausted
if (lstate.block_offset >= total_rows) {
    if (!GetNextBlock(context, lstate, gstate)) {
        return;  // Done
    }
}

// Calculate output size (up to STANDARD_VECTOR_SIZE)
idx_t remaining_rows = total_rows - lstate.block_offset;
idx_t output_size = MinValue<idx_t>(STANDARD_VECTOR_SIZE, remaining_rows);

// Convert Block → DataChunk
ClickhouseConversion::BlockToDuckDB(
    lstate.aux_data->block, output, 
    lstate.block_offset, output_size,
    column_ids, bind_data.column_types, 
    lstate.aux_data);

lstate.block_offset += output_size;
```

### 5. Block Fetching (`ClickhouseScanFunction::GetNextBlock`)

**Location**: `src/clickhouse_scan.cpp:121`

**Purpose**: Thread-safe block retrieval

**Synchronization**:
```cpp
lock_guard<mutex> lock(global_state.result_mutex);

if (global_state.done) {
    return false;
}

auto block_opt = global_state.result->Next();
if (!block_opt.has_value()) {
    global_state.done = true;
    return false;
}

// Store block and create auxiliary data
local_state.current_block = block;
local_state.aux_data = make_shared_ptr<ClickhouseAuxiliaryData>(std::move(block));
```

## Zero-Copy Conversion

### Numeric Types

**Template**: `ConvertNumericColumnTemplated<CH_TYPE, DUCK_TYPE>`

**Strategy**:
- Direct access via `ColumnVector<T>`
- Fast iteration with type casting
- Auxiliary data keeps Block alive

```cpp
auto ch_typed = ch_column->As<clickhouse::ColumnVector<CH_TYPE>>();
auto duck_data = FlatVector::GetData<DUCK_TYPE>(output);

for (idx_t i = 0; i < count; i++) {
    duck_data[i] = static_cast<DUCK_TYPE>((*ch_typed)[offset + i]);
}
```

**Supported Mappings**:
| ClickHouse Type | DuckDB Type | Strategy |
|-----------------|-------------|----------|
| UInt8           | UTINYINT    | Direct cast |
| Int8            | TINYINT     | Direct cast |
| UInt16          | USMALLINT   | Direct cast |
| Int16           | SMALLINT    | Direct cast |
| UInt32          | UINTEGER    | Direct cast |
| Int32           | INTEGER     | Direct cast |
| UInt64          | UBIGINT     | Direct cast |
| Int64           | BIGINT      | Direct cast |
| Float32         | FLOAT       | Direct cast |
| Float64         | DOUBLE      | Direct cast |

### String Types

**Function**: `ConvertStringColumn`

**Strategy**: Copy strings into DuckDB string storage

```cpp
auto ch_string_col = nested_column->As<clickhouse::ColumnString>();
auto result_data = FlatVector::GetData<string_t>(output);

for (idx_t i = 0; i < count; i++) {
    auto ch_str = ch_string_col->At(offset + i);
    result_data[i] = StringVector::AddString(output, ch_str.data(), ch_str.size());
}
```

### Date/DateTime Types

**Date**: ClickHouse Date = days since Unix epoch (same as DuckDB!)
```cpp
auto ch_date = nested_column->As<clickhouse::ColumnDate>();
for (idx_t i = 0; i < count; i++) {
    auto days = ch_date->At(offset + i);
    result_data[i] = date_t(static_cast<int32_t>(days));
}
```

**DateTime**: ClickHouse DateTime = seconds, DuckDB TIMESTAMP = microseconds
```cpp
auto ch_datetime = nested_column->As<clickhouse::ColumnDateTime>();
for (idx_t i = 0; i < count; i++) {
    auto unix_ts = ch_datetime->At(offset + i);
    result_data[i] = Timestamp::FromEpochSeconds(static_cast<int64_t>(unix_ts));
}
```

### NULL Handling

**Strategy**: Check for `ColumnNullable` wrapper

```cpp
auto *nullable = dynamic_cast<clickhouse::ColumnNullable *>(ch_column.get());
if (nullable) {
    nested_column = nullable->Nested();
}

// ... perform conversion on nested_column ...

if (nullable) {
    auto &validity = FlatVector::Validity(output);
    validity.EnsureWritable();
    for (idx_t i = 0; i < count; i++) {
        if (nullable->IsNull(offset + i)) {
            validity.SetInvalid(i);
        }
    }
}
```

## Filter Pushdown

### Supported Filter Types

**Implementation**: `src/clickhouse_filter_pushdown.cpp`

1. **IS NULL / IS NOT NULL**
   ```sql
   column_name IS NULL
   column_name IS NOT NULL
   ```

2. **Constant Comparisons**
   ```sql
   column_name = value
   column_name != value
   column_name < value
   column_name <= value
   column_name > value
   column_name >= value
   ```

3. **Conjunctions**
   ```sql
   (filter1 AND filter2)
   (filter1 OR filter2)
   ```

4. **IN Filter**
   ```sql
   column_name IN (value1, value2, value3)
   ```

### Constant Transformation

**Function**: `TransformConstant`

```cpp
switch (val.type().id()) {
case LogicalTypeId::BOOLEAN:
case LogicalTypeId::INTEGER:
case LogicalTypeId::BIGINT:
case LogicalTypeId::FLOAT:
case LogicalTypeId::DOUBLE:
    return val.ToString();  // Direct numeric conversion
    
case LogicalTypeId::VARCHAR:
    return ClickhouseUtils::WriteLiteral(StringValue::Get(val));
    // Properly escapes and quotes strings
    
case LogicalTypeId::DATE:
case LogicalTypeId::TIMESTAMP:
    return ClickhouseUtils::WriteLiteral(val.ToString());
}
```

### Example Transformations

| DuckDB Filter | ClickHouse WHERE |
|---------------|------------------|
| `id > 100` | `id > 100` |
| `name = 'Alice'` | `name = 'Alice'` |
| `id > 1 AND value < 200` | `(id > 1 AND value < 200)` |
| `id IN (1, 2, 3)` | `id IN (1, 2, 3)` |
| `created_at >= '2024-01-01'` | `created_at >= '2024-01-01'` |
| `name IS NOT NULL` | `name IS NOT NULL` |

## Multi-Threading

### Thread Pool Configuration

**Global State**:
```cpp
idx_t ClickhouseScanGlobalState::MaxThreads() const {
    return 8;  // Up to 8 parallel scan threads
}
```

### Thread Synchronization

**Block Distribution**:
- Global state holds single `ClickhouseResult` with mutex
- Each thread calls `GetNextBlock` to fetch next available block
- Mutex ensures only one thread fetches at a time
- Threads process blocks independently once fetched

**Concurrency Model**:
```
Global State (with mutex)
    │
    ├─► Thread 1 → Block 0 → Process → Block 3 → ...
    ├─► Thread 2 → Block 1 → Process → Block 4 → ...
    ├─► Thread 3 → Block 2 → Process → Block 5 → ...
    └─► ...
```

### Performance Considerations

1. **Async Query Execution**: ClickHouse query runs in separate thread via `ClickhouseClient`
2. **Channel Buffering**: Results buffered via `msd::channel` (size: 10)
3. **Batch Processing**: Process full blocks (not row-by-row)
4. **Zero-Copy Numerics**: Minimize data copying for numeric types

## Testing

### Test File: `test/sql/clickhouse_scan.test`

**Test Categories**:

1. **Basic Functionality**
   - Full table scan
   - Column projection
   - Row filtering

2. **Pushdown Verification**
   - Projection: `SELECT id, name FROM ...`
   - Filter: `WHERE id > 1`
   - Combined: `SELECT name WHERE value > 100`

3. **Data Types**
   - Numerics: INT8, INT16, INT32, INT64, UINT*, FLOAT, DOUBLE
   - Strings: VARCHAR
   - Dates: DATE, TIMESTAMP

4. **NULL Handling**
   - IS NULL / IS NOT NULL filters
   - NULL value conversion

5. **Complex Queries**
   - AND/OR filters
   - IN filters
   - Aggregations (COUNT, SUM)
   - ORDER BY
   - LIMIT

### Test Setup

**Prerequisites**:
```sql
-- ClickHouse server running on localhost:9000
-- Database: test_db
-- Table: t1 with schema:
--   id UBIGINT
--   name VARCHAR
--   value DOUBLE
--   created_at DATE
```

**Sample Test**:
```sql
# Basic scan
query IIII rowsort
SELECT * FROM ch.test_db.t1;
----
1	Alice	99.5	2024-01-01
2	Bob	150.25	2024-01-02
3	Charlie	200.0	2024-01-03

# Filter pushdown
query II rowsort
SELECT id, name FROM ch.test_db.t1 WHERE value > 100;
----
2	Bob
3	Charlie
```

## Files Created/Modified

### New Files

1. **`src/include/clickhouse_conversion.hpp`**
   - Conversion function declarations
   - `ClickhouseAuxiliaryData` definition

2. **`src/clickhouse_conversion.cpp`**
   - Block to Vector conversion implementation
   - Type-specific converters

3. **`src/include/clickhouse_filter_pushdown.hpp`**
   - Filter pushdown interface

4. **`src/clickhouse_filter_pushdown.cpp`**
   - Filter transformation logic

5. **`test/sql/clickhouse_scan.test`**
   - Comprehensive test suite

### Modified Files

1. **`src/include/clickhouse_scan.hpp`**
   - Added bind data structures
   - Added global/local state structures
   - Updated function signatures

2. **`src/clickhouse_scan.cpp`**
   - Complete scan function implementation
   - Multi-threading support
   - Pushdown integration

3. **`src/storage/clickhouse_table_entry.cpp`**
   - Updated `GetScanFunction` to create bind data

4. **`src/include/clickhouse_client.hpp`**
   - Added `#pragma once` include guard

5. **`src/CMakeLists.txt`**
   - Added new source files to build

## Limitations

### Current Implementation

1. **Type Support**: Limited to numeric, string, date, datetime
   - No Array/Map/Struct types
   - No Decimal types
   - No UUID types

2. **Filter Pushdown**: Basic filters only
   - No UDF pushdown
   - No complex expressions (LIKE, BETWEEN, etc.)

3. **Read-Only**: Write operations not implemented

### Future Enhancements

1. **Extended Type Support**
   - Decimal types
   - Array/Map types
   - UUID types

2. **Advanced Filters**
   - LIKE patterns
   - BETWEEN clauses
   - EXISTS subqueries

3. **Optimizations**
   - Adaptive channel buffer sizing
   - Column-wise compression awareness
   - Statistics collection

## References

- **Arrow Table Function**: `duckdb/src/function/table/arrow.cpp`
  - Template for multi-threaded scanning
  - Projection/filter pushdown patterns

- **MySQL Filter Pushdown**: `duckdb-mysql/src/mysql_filter_pushdown.cpp`
  - Filter transformation logic
  - Constant value formatting

- **ClickHouse C++ Client**: `vcpkg:clickhouse-cpp`
  - Column API: `ColumnVector<T>`, `ColumnString`, `ColumnDate`, etc.
  - Block structure and iteration

## Build Instructions

```bash
# Install dependencies (if needed)
vcpkg install

# Build debug version
GEN=ninja make debug

# Run tests
./build/debug/test/unittest "test/sql/clickhouse_scan.test"
```

## Code Style Compliance

- **Indentation**: TABS (TabWidth: 4)
- **Naming**: 
  - Classes: `CamelCase`
  - Functions: `CamelCase`
  - Variables: `lower_case`
- **Pointers**: RIGHT alignment (`Type *ptr`)
- **Includes**: Manual order (no auto-sort)
- **Braces**: Required around statements

All code follows `.clang-format` and `.clang-tidy` configurations.
