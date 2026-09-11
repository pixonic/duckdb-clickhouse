
#include "duckdb/parser/parsed_data/create_table_info.hpp"
#include "duckdb/parser/constraints/not_null_constraint.hpp"
#include "duckdb/parser/parser.hpp"

#include "storage/clickhouse_table_set.hpp"
#include "storage/clickhouse_table_entry.hpp"
#include "clickhouse_utils.hpp"
#include "clickhouse_types.hpp"

#include <clickhouse/client.h>

namespace ch = clickhouse;

namespace duckdb {

static void AddColumn(const ch::Block &block, idx_t row_idx, CreateTableInfo &info,
                      vector<ClickhouseColumnDefinition> &columns) {
	auto column_name = string(block[1]->As<ch::ColumnString>()->At(row_idx));
	auto raw_type = string(block[2]->As<ch::ColumnString>()->At(row_idx));
	auto raw_default_expr = string(block[3]->As<ch::ColumnString>()->At(row_idx));

	int64_t numeric_precision = -1;

	auto precision_col = block[4]->As<ch::ColumnNullable>();
	if (!precision_col->IsNull(row_idx)) {
		numeric_precision = static_cast<int64_t>(precision_col->Nested()->As<ch::ColumnUInt64>()->At(row_idx));
	}

	int64_t numeric_scale = -1;

	auto scale_col = block[5]->As<ch::ColumnNullable>();
	if (!scale_col->IsNull(row_idx)) {
		numeric_scale = static_cast<int64_t>(scale_col->Nested()->As<ch::ColumnUInt64>()->At(row_idx));
	}

	int64_t datetime_precision = -1;

	auto datetime_precision_col = block[6]->As<ch::ColumnNullable>();
	if (!datetime_precision_col->IsNull(row_idx)) {
		datetime_precision =
		    static_cast<int64_t>(datetime_precision_col->Nested()->As<ch::ColumnUInt64>()->At(row_idx));
	}

	ClickhouseTypeData type_data {raw_type, numeric_precision, numeric_scale, datetime_precision};

	auto column_type_opt = ClickhouseTypes::TypeToLogicalType(type_data);
	auto column_type = column_type_opt.value_or(LogicalType::BLOB);

	ClickhouseColumnDefinition ch_column {column_name, std::move(raw_type), column_type_opt.has_value()};
	columns.push_back(std::move(ch_column));

	ColumnDefinition column(std::move(column_name), std::move(column_type));
	if (!raw_default_expr.empty()) {
		auto expressions = Parser::ParseExpressionList(raw_default_expr);
		if (!expressions.empty()) {
			column.SetDefaultValue(std::move(expressions[0]));
		}
	}

	auto nullable = ClickhouseTypes::IsNullable(type_data);

	if (!nullable) {
		auto column_idx = info.columns.LogicalColumnCount();
		info.constraints.push_back(make_uniq<NotNullConstraint>(LogicalIndex(column_idx)));
	}

	info.columns.AddColumn(std::move(column));
}

ClickhouseTableSet::ClickhouseTableSet(SchemaCatalogEntry &schema, Catalog &catalog)
    : ClickhouseCatalogSet(catalog), schema(schema) {
}

void ClickhouseTableSet::LoadEntries(ClickhouseTransaction &transaction) {
	auto query = StringUtil::Replace(R"(
SELECT table, name, type, default_expression, numeric_precision, numeric_scale, datetime_precision 
FROM system.columns
WHERE database=${SCHEMA_NAME}
ORDER BY table, position;
)",
	                                 "${SCHEMA_NAME}", ClickhouseUtils::WriteLiteral(schema.name));

	vector<unique_ptr<CreateTableInfo>> tables;
	vector<vector<ClickhouseColumnDefinition>> table_columns;

	unique_ptr<CreateTableInfo> info;
	vector<ClickhouseColumnDefinition> columns;

	auto client = transaction.NewClient();
	auto result = client->Query(query);

	while (true) {
		auto block_opt = result.Next();
		if (!block_opt.has_value()) {
			break;
		}
		const auto &block = block_opt.value();

		for (auto i = 0; i < block.GetRowCount(); i++) {
			auto table_name = block[0]->As<ch::ColumnString>()->At(i);

			if (!info || info->table != table_name) {
				if (info) {
					tables.push_back(std::move(info));
					table_columns.push_back(columns);
				}
				info = make_uniq<CreateTableInfo>(schema, string(table_name));
				columns.clear();
			}
			AddColumn(block, i, *info, columns);
		}
	}

	if (info) {
		tables.push_back(std::move(info));
		table_columns.push_back(columns);
	}

	for (idx_t i = 0; i < tables.size(); i++) {
		auto table_entry = make_uniq<ClickhouseTableEntry>(catalog, schema, *tables[i], table_columns[i]);
		CreateEntry(std::move(table_entry));
	}
}

} // namespace duckdb
