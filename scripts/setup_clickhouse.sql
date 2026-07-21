-- SQL code to initialize ClickHouse with test data
DROP DATABASE IF EXISTS test_db;
CREATE DATABASE test_db;

CREATE TABLE test_db.t1 (
    id UInt64,
    name String,
    value Float64,
    created_at DateTime('UTC')
) ENGINE = MergeTree() ORDER BY id;

INSERT INTO test_db.t1 VALUES
    (1, 'Alice', 99.5, '2024-01-01 00:00:00'),
    (2, 'Bob', 150.25, '2024-01-02 00:00:00'),
    (3, 'Charlie', 200.0, '2024-01-03 00:00:00');

CREATE TABLE test_db.supported_types (
    bool_value Bool,
    uint8_value UInt8,
    int8_value Int8,
    uint16_value UInt16,
    int16_value Int16,
    uint32_value UInt32,
    int32_value Int32,
    uint64_value UInt64,
    int64_value Int64,
    float32_value Float32,
    float64_value Float64,
    string_value String,
    fixed_string_value FixedString(5),
    date_value Date,
    datetime_value DateTime('UTC')
) ENGINE = MergeTree() ORDER BY uint8_value;

INSERT INTO test_db.supported_types VALUES (
    true,
    255,
    -128,
    65535,
    -32768,
    4294967295,
    -2147483648,
    18446744073709551615,
    -9223372036854775808,
    1.5,
    -2.25,
    'clickhouse',
    'fixed',
    '2024-02-29',
    '2024-02-29 12:34:56'
);

CREATE TABLE test_db.nullable_types (
    row_id UInt8,
    bool_value Nullable(Bool),
    uint8_value Nullable(UInt8),
    int8_value Nullable(Int8),
    uint16_value Nullable(UInt16),
    int16_value Nullable(Int16),
    uint32_value Nullable(UInt32),
    int32_value Nullable(Int32),
    uint64_value Nullable(UInt64),
    int64_value Nullable(Int64),
    float32_value Nullable(Float32),
    float64_value Nullable(Float64),
    string_value Nullable(String),
    fixed_string_value Nullable(FixedString(5)),
    date_value Nullable(Date),
    datetime_value Nullable(DateTime('UTC'))
) ENGINE = MergeTree() ORDER BY row_id;

INSERT INTO test_db.nullable_types VALUES
    (1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
    (2, false, 42, -42, 4242, -4242, 424242, -424242, 42424242, -42424242, 3.5, -4.5,
     'nullable', 'value', '2023-12-31', '2023-12-31 23:59:58');

-- 4097 rows exercise two full DuckDB standard vectors (2048 rows each)
-- and a one-row remainder from a single ClickHouse block.
CREATE TABLE test_db.vector_size (
    id UInt64,
    value Int64,
    label String,
    nullable_id Nullable(UInt64)
) ENGINE = MergeTree() ORDER BY id;

INSERT INTO test_db.vector_size
SELECT
    number,
    toInt64(number) - 2048,
    concat('row-', toString(number)),
    if(number % 2 = 0, NULL, number)
FROM numbers(4097);

-- More than 256 distinct strings exercise a multi-byte LowCardinality index,
-- while 4097 rows also cross DuckDB standard-vector boundaries.
CREATE TABLE test_db.low_cardinality_types (
    row_id UInt64,
    category LowCardinality(String),
    nullable_category LowCardinality(Nullable(String))
) ENGINE = MergeTree() ORDER BY row_id;

INSERT INTO test_db.low_cardinality_types
SELECT
    number,
    if(number % 301 = 300, '', concat('category-', toString(number % 301))),
    if(number % 5 = 0, NULL, concat('nullable-', toString(number % 17)))
FROM numbers(4097);
