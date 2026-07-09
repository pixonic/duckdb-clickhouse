-- SQL code to initialize ClickHouse with test data
DROP DATABASE IF EXISTS test_db;
CREATE DATABASE test_db;

CREATE TABLE test_db.t1 (
    id UInt64,
    name String,
    value Float64,
    created_at DateTime
) ENGINE = MergeTree() ORDER BY id;

INSERT INTO test_db.t1 VALUES (1, 'foo', 1.1, '2023-01-01 00:00:00'), (2, 'bar', 2.2, '2023-01-02 00:00:00');
