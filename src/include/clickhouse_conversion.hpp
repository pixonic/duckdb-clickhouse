#pragma once

#include "duckdb/common/types.hpp"
#include "duckdb/common/types/vector_buffer.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/common/types/data_chunk.hpp"

#include <clickhouse/client.h>

namespace duckdb {

// Auxiliary data structure to manage ClickHouse column lifetime for zero-copy conversion
struct ClickhouseAuxiliaryData : public VectorAuxiliaryData {
	static constexpr const VectorAuxiliaryDataType TYPE = VectorAuxiliaryDataType::ARROW_AUXILIARY;

	explicit ClickhouseAuxiliaryData(clickhouse::ColumnRef column_p)
	    : VectorAuxiliaryData(VectorAuxiliaryDataType::ARROW_AUXILIARY), column(std::move(column_p)) {
	}

	~ClickhouseAuxiliaryData() override = default;

	clickhouse::ColumnRef column;
};

class ClickhouseConversion {
public:
	static void BlockToDuckDB(clickhouse::Block &block, DataChunk &output, idx_t block_offset, idx_t count);
};

} // namespace duckdb
