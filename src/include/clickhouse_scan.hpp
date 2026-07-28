#pragma once

#include "duckdb/function/table_function.hpp"
#include "duckdb/common/mutex.hpp"
#include "clickhouse_client.hpp"
#include "clickhouse_conversion.hpp"

namespace duckdb {

class ClickhouseTableEntry;

// Bind data for ClickHouse scan function
struct ClickhouseScanBindData : public FunctionData {
	explicit ClickhouseScanBindData(ClickhouseTableEntry &table) : table(table) {
	}

	ClickhouseTableEntry &table;

	vector<string> column_names;
	vector<LogicalType> column_types;
	vector<string> source_column_types;

	unique_ptr<FunctionData> Copy() const override;
	bool Equals(const FunctionData &other_p) const override;
};

// Global state for parallel scanning
struct ClickhouseScanGlobalState : public GlobalTableFunctionState {
	explicit ClickhouseScanGlobalState(unique_ptr<ClickhouseResult> result_p) : result(std::move(result_p)) {
	}

	unique_ptr<ClickhouseResult> result;
	mutex result_mutex;
	bool done = false;
	idx_t batch_index = 0;

	idx_t MaxThreads() const override;
};

// Local state for each thread
struct ClickhouseScanLocalState : public LocalTableFunctionState {
	ClickhouseScanLocalState() : block_offset(0), batch_index(0) {
	}

	std::optional<clickhouse::Block> current_block;
	idx_t block_offset;
	idx_t batch_index;

	void Reset();
};

class ClickhouseScanFunction : public TableFunction {
public:
	ClickhouseScanFunction();

	static unique_ptr<FunctionData> Bind(ClientContext &context, TableFunctionBindInput &input,
	                                     vector<LogicalType> &return_types, vector<string> &names);

	static unique_ptr<GlobalTableFunctionState> InitGlobal(ClientContext &context, TableFunctionInitInput &input);

	static unique_ptr<LocalTableFunctionState> InitLocal(ExecutionContext &context, TableFunctionInitInput &input,
	                                                     GlobalTableFunctionState *global_state);

	static void Scan(ClientContext &context, TableFunctionInput &data, DataChunk &output);

	static bool GetNextBlock(ClientContext &context, ClickhouseScanLocalState &local_state,
	                         ClickhouseScanGlobalState &global_state);
};

} // namespace duckdb
