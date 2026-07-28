# Add ClickHouse Array Support

  ## Summary

  Map variable-length ClickHouse Array(T) columns to DuckDB LIST/T[]. Support recursive arrays containing every currently supported scalar type, nullable elements, LowCardinality
  strings, and nested arrays. clickhouse-cpp 2.6.2 already provides the required flattened data and offset APIs; no dependency upgrade is needed.

  ## Implementation Changes

  - Extend type mapping to recognize outer wrappers structurally instead of using substring matches:
      - Map Array(T) recursively to LogicalType::LIST(mapped_T).
      - Derive nested temporal precision from the nested type declaration.
      - Mark the parent column nullable only for top-level Nullable or LowCardinality(Nullable(...)); Array(Nullable(T)) remains a non-null list with nullable children.
      - Continue rejecting unsupported element types through the existing unsupported-type error path.

  - Add recursive ColumnArray conversion:
      - Copy selected row boundaries into DuckDB list_entry_t values with offsets relative to the output child vector.
      - Convert the corresponding contiguous slice of ColumnArray::GetData() through the existing conversion dispatcher.
      - Preserve nullable child validity, nested arrays, temporal conversions, LowCardinality strings, and leaf-column lifetime management.
      - Handle empty arrays and zero-child batches without invoking unsafe zero-length leaf conversions.
      - Validate source bounds and expected ClickHouse/DuckDB column types before conversion.

  - Preserve each column’s exact system.columns.type declaration in the table entry and scan bind data. This is an internal constructor/data-interface change with no new SQL API.
  - Extend filter serialization for DuckDB LIST constants:
      - Recursively emit ClickHouse array literals, including nested lists and NULL elements.
      - Wrap every top-level array constant in CAST(<literal>, '<exact ClickHouse type>'). The retained source declaration is required for FixedString, Date, Date32, temporal
        precision/timezones, nullable elements, empty arrays, and LowCardinality elements.

      - Apply this to =, !=, <, <=, >, >=, and IN table filters.
      - Leave function predicates such as list_contains to DuckDB unless the optimizer already represents them as supported table filters.

  ## Test Plan

  - Add ClickHouse fixtures covering arrays of all currently supported scalar families: Boolean, signed/unsigned integers, floats, String, FixedString, Date/Date32, DateTime/
    DateTime64, Time/Time64, and LowCardinality strings.

  - Verify catalog exposure as DuckDB T[], including Array(Nullable(T)) being non-null at the column level while preserving NULL elements.
  - Verify empty arrays, nullable elements, nested arrays, nested nullable leaves, temporal precision/timezones, and LowCardinality values.
  - Add a 4,097-row variable-length array fixture with empty and nullable values to exercise both parent and flattened-child offsets across DuckDB’s 2,048-row vector boundaries.
  - Test pushed equality, inequality, ordering, and IN predicates, including filter-only projected columns and constants for empty, nested, nullable, FixedString, Date, and high-
    precision temporal arrays.

  - Run release/debug targeted SQLLogicTests, the full test suite
  - Don't make tidy-check-fixed, because it seems to be broken

  ## Assumptions

  - ClickHouse arrays remain variable-length DuckDB LIST values, not fixed-length DuckDB ARRAY values.
  - Support is limited recursively to element types already supported by the scanner; Tuple, Map, Decimal, and other unsupported child types remain out of scope.
  - ClickHouse does not allow a nullable outer Array, but nullable elements are fully supported.
  - Existing scalar conversion and temporal timezone semantics remain unchanged.
  