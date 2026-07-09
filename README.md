# DuckDB ClickHouse extension

> [!WARNING]
> This project is currently in a Work-In-Progress (WIP) state. It is not recommended for use in production environments.

The ClickHouse extension allows DuckDB to directly read data from a ClickHouse database instance. The data can be queried directly from the underlying ClickHouse database.

## Reading Data from ClickHouse

To make a ClickHouse database accessible to DuckDB use the `ATTACH` command:

```sql
ATTACH 'host=localhost port=9000 database=default' AS ch (TYPE clickhouse_scanner);
USE ch;
```

The connection string determines the parameters for how to connect to ClickHouse as a set of `key=value` pairs separated by spaces.

| Setting  |        Description         | Default    |
|----------|----------------------------|------------|
| host     | Name of host to connect to | localhost  |
| user     | ClickHouse user name       | default    |
| password | ClickHouse password        |            |
| database | Database name              | default    |
| port     | Port number (native)       | 9000       |

The tables in the ClickHouse database can be read as if they were normal DuckDB tables, but the underlying data is read directly from ClickHouse at query time.

```sql
D SHOW TABLES;
┌─────────┐
│  name   │
│ varchar │
├─────────┤
│ t1      │
└─────────┘
D SELECT * FROM t1;
```

## Development

#### Dependencies

The package depends on `vcpkg`, and has several platform-specific dependencies that must be installed in order for compilation to succeed.

##### Submodules

The DuckDB submodule must be initialized prior to building.

```bash
git submodule update --init --recursive
```

##### vcpkg

`vcpkg` must be installed and configured for building.

```bash
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
export VCPKG_TOOLCHAIN_PATH=`pwd`/vcpkg/scripts/buildsystems/vcpkg.cmake
```

## Building & Loading the Extension
To build, type:

```bash
make
```

To run, run the bundled `duckdb` shell:
```bash
./build/release/duckdb -unsigned
```

Then, load the ClickHouse extension like so:
```sql
LOAD 'build/release/extension/clickhouse_scanner/clickhouse_scanner.duckdb_extension';
```

## Testing

Tests can be run with the following command:

```bash
make test
```

Note that most tests require a ClickHouse server running. You can initialize the test database using the provided setup script:

```bash
clickhouse-client < scripts/setup_clickhouse.sql
```

## License

The source code in this repository is published under the [MIT license](./LICENSE).