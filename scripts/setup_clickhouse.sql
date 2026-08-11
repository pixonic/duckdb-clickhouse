-- SQL code to initialize ClickHouse with test data
SET enable_time_time64_type = 1;

DROP DATABASE IF EXISTS test_db;
CREATE DATABASE test_db;

CREATE TABLE test_db.empty (id UInt64) ENGINE = MergeTree() ORDER BY id;

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

-- UUIDs whose native ClickHouse order is the reverse of their canonical
-- string order are included to verify order-preserving filter pushdown.
CREATE TABLE test_db.uuid_types (
    row_id UInt8,
    uuid_value UUID,
    nullable_uuid Nullable(UUID)
) ENGINE = MergeTree() ORDER BY row_id;

INSERT INTO test_db.uuid_types VALUES
    (1, '00000000-0000-0000-0000-000000000000', NULL),
    (2, '00000000-0000-0000-0000-000000000001', '00000000-0000-0000-0000-000000000001'),
    (3, '00000000-0000-0001-0000-000000000000', '00000000-0000-0001-0000-000000000000'),
    (4, '00112233-4455-6677-8899-aabbccddeeff', '00112233-4455-6677-8899-aabbccddeeff'),
    (5, 'ffffffff-ffff-ffff-ffff-ffffffffffff', 'ffffffff-ffff-ffff-ffff-ffffffffffff');

-- 4097 rows exercise two full DuckDB standard vectors (2048 rows each)
-- and a one-row remainder from a single ClickHouse block.
CREATE TABLE test_db.vector_size (
    id UInt64,
    value Int64,
    label String,
    nullable_id Nullable(UInt64),
    uuid_value UUID,
    nullable_uuid Nullable(UUID)
) ENGINE = MergeTree() ORDER BY id;

INSERT INTO test_db.vector_size
SELECT
    number,
    toInt64(number) - 2048,
    concat('row-', toString(number)),
    if(number % 2 = 0, NULL, number),
    reinterpretAsUUID(toUInt128(number)),
    if(number % 2 = 0, NULL, reinterpretAsUUID(toUInt128(number)))
FROM numbers(4097);

-- Every DateTime64 and Time64 precision is present so catalog precision
-- bucketing is covered independently from value conversion tests.
CREATE TABLE test_db.temporal_precision_types (
    datetime64_0 DateTime64(0, 'UTC'),
    datetime64_1 DateTime64(1, 'UTC'),
    datetime64_2 DateTime64(2, 'UTC'),
    datetime64_3 DateTime64(3, 'UTC'),
    datetime64_4 DateTime64(4, 'UTC'),
    datetime64_5 DateTime64(5, 'UTC'),
    datetime64_6 DateTime64(6, 'UTC'),
    datetime64_7 DateTime64(7, 'UTC'),
    datetime64_8 DateTime64(8, 'UTC'),
    datetime64_9 DateTime64(9, 'UTC'),
    time_value Time,
    time64_0 Time64(0),
    time64_1 Time64(1),
    time64_2 Time64(2),
    time64_3 Time64(3),
    time64_4 Time64(4),
    time64_5 Time64(5),
    time64_6 Time64(6),
    time64_7 Time64(7),
    time64_8 Time64(8),
    time64_9 Time64(9)
) ENGINE = MergeTree() ORDER BY tuple();

CREATE TABLE test_db.temporal_values (
    row_id UInt8,
    date32_value Date32,
    datetime64_s DateTime64(0, 'UTC'),
    datetime64_ms DateTime64(3, 'UTC'),
    datetime64_us DateTime64(6, 'UTC'),
    datetime64_ns DateTime64(9, 'UTC'),
    datetime64_tokyo DateTime64(3, 'Asia/Tokyo'),
    time_value Time,
    time64_us Time64(6),
    time64_ns Time64(9),
    nullable_date32 Nullable(Date32),
    nullable_datetime64 Nullable(DateTime64(8, 'UTC')),
    nullable_time64 Nullable(Time64(8))
) ENGINE = MergeTree() ORDER BY row_id;

INSERT INTO test_db.temporal_values VALUES
    (1, '1960-01-02', '1969-12-31 23:59:59', '1969-12-31 23:59:58.765',
     '1969-12-31 23:59:58.765432', '1969-12-31 23:59:58.765432110',
     '1970-01-01 00:00:00.000', '00:00:00', '01:02:03.123456', '01:02:03.123456789', NULL, NULL, NULL),
    (2, '2024-02-29', '2024-02-29 12:34:56', '2024-02-29 12:34:56.123',
     '2024-02-29 12:34:56.123456', '2024-02-29 12:34:56.123456789',
     '2024-02-29 12:34:56.789', '24:00:00', '12:34:56.654321', '23:59:59.999999999',
     '2024-03-01', '2024-02-29 12:34:56.12345678', '12:34:56.12345678');

CREATE TABLE test_db.invalid_time_values (
    row_id UInt8,
    time_value Time64(9)
) ENGINE = MergeTree() ORDER BY row_id;

INSERT INTO test_db.invalid_time_values VALUES
    (1, '-01:00:00'),
    (2, '25:00:00');

CREATE TABLE test_db.invalid_datetime64_values (
    datetime64_7 DateTime64(7, 'UTC'),
    datetime64_8 DateTime64(8, 'UTC')
) ENGINE = MergeTree() ORDER BY tuple();

INSERT INTO test_db.invalid_datetime64_values VALUES
    ('2299-12-31 23:59:59.1234567', '2299-12-31 23:59:59.12345678');

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

CREATE TABLE test_db.array_types (
    row_id UInt8,
    bool_values Array(Bool),
    uint8_values Array(UInt8),
    int8_values Array(Int8),
    uint16_values Array(UInt16),
    int16_values Array(Int16),
    uint32_values Array(UInt32),
    int32_values Array(Int32),
    uint64_values Array(UInt64),
    int64_values Array(Int64),
    float32_values Array(Float32),
    float64_values Array(Float64),
    string_values Array(String),
    fixed_string_values Array(FixedString(5)),
    date_values Array(Date),
    date32_values Array(Date32),
    datetime_values Array(DateTime('UTC')),
    datetime64_values Array(DateTime64(9, 'UTC')),
    time_values Array(Time),
    time64_values Array(Time64(9)),
    low_cardinality_values Array(LowCardinality(String)),
    nullable_low_cardinality_values Array(LowCardinality(Nullable(String)))
) ENGINE = MergeTree() ORDER BY row_id;

INSERT INTO test_db.array_types VALUES (
    1,
    [true, false],
    [0, 255],
    [-128, 127],
    [0, 65535],
    [-32768, 32767],
    [0, 4294967295],
    [-2147483648, 2147483647],
    [0, 18446744073709551615],
    [-9223372036854775808, 9223372036854775807],
    [1.5, -2.5],
    [3.25, -4.5],
    ['clickhouse', 'array'],
    ['fixed', 'value'],
    ['1970-01-01', '2024-02-29'],
    ['1960-01-02', '2024-03-01'],
    ['1970-01-01 00:00:00', '2024-02-29 12:34:56'],
    ['1969-12-31 23:59:58.765432110', '2024-02-29 12:34:56.123456789'],
    ['00:00:00', '24:00:00'],
    ['01:02:03.123456789', '23:59:59.999999999'],
    ['low', 'cardinality'],
    ['nullable', NULL]
);

CREATE TABLE test_db.array_features (
    row_id UInt8,
    nullable_values Array(Nullable(Int32)),
    nested_values Array(Array(Nullable(String))),
    fixed_values Array(FixedString(3)),
    date_values Array(Date),
    datetime_values Array(DateTime64(9, 'UTC')),
    uuid_values Array(UUID),
    nullable_uuid_values Array(Nullable(UUID)),
    nested_uuid_values Array(Array(UUID))
) ENGINE = MergeTree() ORDER BY row_id;

INSERT INTO test_db.array_features VALUES
    (1, [], [], [], [], [], [], [], []),
    (2, [1, NULL, 3], [['a', NULL], [], ['c']], ['abc', 'xyz'], ['1970-01-01'],
     ['1969-12-31 23:59:58.765432110'],
     ['00000000-0000-0000-0000-000000000001'],
     ['00000000-0000-0000-0000-000000000000', NULL,
      '00112233-4455-6677-8899-aabbccddeeff', 'ffffffff-ffff-ffff-ffff-ffffffffffff'],
     [['00000000-0000-0000-0000-000000000001']]),
    (3, [2, 4], [['z']], ['zzz'], ['2024-02-29'], ['2024-02-29 12:34:56.123456789'],
     ['00000000-0000-0001-0000-000000000000'],
     ['00000000-0000-0001-0000-000000000000'],
     [['00000000-0000-0001-0000-000000000000']]);

-- Variable parent and nullable-child lengths cross both 2,048-row output
-- boundaries while arriving in one ClickHouse block.
CREATE TABLE test_db.array_vector_size (
    row_id UInt64,
    values Array(Nullable(Int64))
) ENGINE = MergeTree() ORDER BY row_id;

INSERT INTO test_db.array_vector_size
SELECT
    number,
    arrayMap(x -> if((number + x) % 5 = 0, NULL, toInt64(number + x)), range(toUInt32(number % 4)))
FROM numbers(4097);
