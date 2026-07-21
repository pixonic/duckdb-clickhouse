#include "clickhouse_scan.hpp"
#include "clickhouse_filter_pushdown.hpp"
#include "clickhouse_utils.hpp"
#include "duckdb/main/database_manager.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/common/limits.hpp"

#include "duckdb/common/printer.hpp"

// Forward declarations to avoid circular includes
namespace duckdb {
class ClickhouseTableEntry;
class ClickhouseTransaction;
} // namespace duckdb

#include "storage/clickhouse_table_entry.hpp"
#include "storage/clickhouse_transaction.hpp"

namespace duckdb {

//===--------------------------------------------------------------------===//
// Bind Data
//===--------------------------------------------------------------------===//

unique_ptr<FunctionData> ClickhouseScanBindData::Copy() const {
	throw NotImplementedException("ClickhouseScanBindData copy not supported");
}

bool ClickhouseScanBindData::Equals(const FunctionData &other_p) const {
	return false;
}

//===--------------------------------------------------------------------===//
// Global State
//===--------------------------------------------------------------------===//

idx_t ClickhouseScanGlobalState::MaxThreads() const {
	// Support multi-threaded scanning
	// return 8;
	return 1;
}

//===--------------------------------------------------------------------===//
// Local State
//===--------------------------------------------------------------------===//

void ClickhouseScanLocalState::Reset() {
	block_offset = 0;
	current_block = std::nullopt;
}

//===--------------------------------------------------------------------===//
// Bind
//===--------------------------------------------------------------------===//

unique_ptr<FunctionData> ClickhouseScanFunction::Bind(ClientContext &context, TableFunctionBindInput &input,
                                                      vector<LogicalType> &return_types, vector<string> &names) {
	throw InternalException("ClickhouseScanFunction::Bind should not be called directly - use table catalog");
}

//===--------------------------------------------------------------------===//
// Init Global State
//===--------------------------------------------------------------------===//

unique_ptr<GlobalTableFunctionState> ClickhouseScanFunction::InitGlobal(ClientContext &context,
                                                                        TableFunctionInitInput &input) {
	auto &bind_data = input.bind_data->Cast<ClickhouseScanBindData>();
	auto &table = bind_data.table;

	// Build SELECT statement with projection and filter pushdown
	string select = "SELECT ";

	// Projection pushdown: only select requested columns
	for (idx_t i = 0; i < input.column_ids.size(); i++) {
		if (i > 0) {
			select += ", ";
		}
		auto col_idx = input.column_ids[i];
		auto &col = table.GetColumn(LogicalIndex(col_idx));
		select += ClickhouseUtils::WriteIdentifier(col.GetName());
	}

	select += " FROM ";
	select += ClickhouseUtils::WriteIdentifier(table.schema.name);
	select += ".";
	select += ClickhouseUtils::WriteIdentifier(table.name);

	// Filter pushdown
	string filter_string =
	    ClickhouseFilterPushdown::TransformFilters(input.column_ids, input.filters, bind_data.column_names);
	if (!filter_string.empty()) {
		select += " WHERE " + filter_string;
	}

	// Execute query
	auto &transaction = ClickhouseTransaction::Get(context, table.catalog);
	auto &client = transaction.GetClient();
	auto result = make_uniq<ClickhouseResult>(client.Query(select));

	return make_uniq<ClickhouseScanGlobalState>(std::move(result));
}

//===--------------------------------------------------------------------===//
// Init Local State
//===--------------------------------------------------------------------===//

unique_ptr<LocalTableFunctionState> ClickhouseScanFunction::InitLocal(ExecutionContext &context,
                                                                      TableFunctionInitInput &input,
                                                                      GlobalTableFunctionState *global_state) {
	auto &gstate = global_state->Cast<ClickhouseScanGlobalState>();
	auto local_state = make_uniq<ClickhouseScanLocalState>();

	// Try to get first block
	if (!GetNextBlock(context.client, *local_state, gstate)) {
		return nullptr;
	}

	return local_state;
}

//===--------------------------------------------------------------------===//
// Get Next Block
//===--------------------------------------------------------------------===//

bool ClickhouseScanFunction::GetNextBlock(ClientContext &context, ClickhouseScanLocalState &local_state,
                                          ClickhouseScanGlobalState &global_state) {
	lock_guard<mutex> lock(global_state.result_mutex);

	if (global_state.done) {
		return false;
	}

	while (true) {
		auto block_opt = global_state.result->Next();

		if (!block_opt.has_value()) {
			global_state.done = true;
			return false;
		}

		auto &block = block_opt.value();
		if (block.GetRowCount() == 0) {
			continue;
		}

		// Store block in local state
		local_state.current_block = std::move(block_opt);
		local_state.block_offset = 0;
		local_state.batch_index = ++global_state.batch_index;

		return true;
	}

	return false;
}

//===--------------------------------------------------------------------===//
// Scan
//===--------------------------------------------------------------------===//

void ClickhouseScanFunction::Scan(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &gstate = data.global_state->Cast<ClickhouseScanGlobalState>();
	auto &lstate = data.local_state->Cast<ClickhouseScanLocalState>();
	auto &bind_data = data.bind_data->Cast<ClickhouseScanBindData>();

	if (!lstate.current_block.has_value()) {
		return;
	}

	auto &block = lstate.current_block.value();
	auto total_rows = block.GetRowCount();

	// Check if we've exhausted current block
	if (lstate.block_offset >= total_rows) {
		// Get next block
		if (!GetNextBlock(context, lstate, gstate)) {
			return;
		}
		// Update block reference
		if (!lstate.current_block.has_value()) {
			return;
		}
		total_rows = lstate.current_block.value().GetRowCount();
	}

	// Calculate how many rows to output this iteration
	idx_t remaining_rows = total_rows - lstate.block_offset;
	idx_t output_size = MinValue<idx_t>(STANDARD_VECTOR_SIZE, remaining_rows);

	ClickhouseConversion::BlockToDuckDB(lstate.current_block.value(), output, lstate.block_offset, output_size);

	output.Verify();

	lstate.block_offset += output_size;
}

//===--------------------------------------------------------------------===//
// Table Function
//===--------------------------------------------------------------------===//

ClickhouseScanFunction::ClickhouseScanFunction()
    : TableFunction("clickhouse_scan", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, Scan, Bind,
                    InitGlobal, InitLocal) {
	projection_pushdown = true;
	filter_pushdown = true;
}

} // namespace duckdb
