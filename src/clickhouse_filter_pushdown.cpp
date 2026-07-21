#include "clickhouse_filter_pushdown.hpp"
#include "clickhouse_utils.hpp"
#include "duckdb/planner/filter/conjunction_filter.hpp"
#include "duckdb/planner/filter/constant_filter.hpp"
#include "duckdb/planner/filter/optional_filter.hpp"
#include "duckdb/planner/filter/in_filter.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/interval.hpp"
#include "duckdb/common/types/time.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

string ClickhouseFilterPushdown::CreateExpression(string &column_name, vector<unique_ptr<TableFilter>> &filters,
                                                  string op) {
	vector<string> filter_entries;
	for (auto &filter : filters) {
		auto new_filter = TransformFilter(column_name, *filter);
		if (new_filter.empty()) {
			continue;
		}
		filter_entries.push_back(std::move(new_filter));
	}
	if (filter_entries.empty()) {
		return string();
	}
	return "(" + StringUtil::Join(filter_entries, " " + op + " ") + ")";
}

string ClickhouseFilterPushdown::TransformComparison(ExpressionType type) {
	switch (type) {
	case ExpressionType::COMPARE_EQUAL:
		return "=";
	case ExpressionType::COMPARE_NOTEQUAL:
		return "!=";
	case ExpressionType::COMPARE_LESSTHAN:
		return "<";
	case ExpressionType::COMPARE_GREATERTHAN:
		return ">";
	case ExpressionType::COMPARE_LESSTHANOREQUALTO:
		return "<=";
	case ExpressionType::COMPARE_GREATERTHANOREQUALTO:
		return ">=";
	default:
		throw NotImplementedException("Unsupported expression type");
	}
}

static string TransformConstant(const Value &val) {
	if (val.IsNull()) {
		return "NULL";
	}
	switch (val.type().id()) {
	case LogicalTypeId::BOOLEAN:
	case LogicalTypeId::TINYINT:
	case LogicalTypeId::SMALLINT:
	case LogicalTypeId::INTEGER:
	case LogicalTypeId::BIGINT:
	case LogicalTypeId::UTINYINT:
	case LogicalTypeId::USMALLINT:
	case LogicalTypeId::UINTEGER:
	case LogicalTypeId::UBIGINT:
	case LogicalTypeId::HUGEINT:
	case LogicalTypeId::FLOAT:
	case LogicalTypeId::DOUBLE:
		return val.ToString();
	case LogicalTypeId::VARCHAR:
		return ClickhouseUtils::WriteLiteral(StringValue::Get(val));
	case LogicalTypeId::DATE:
		return ClickhouseUtils::WriteLiteral(val.ToString());
	case LogicalTypeId::TIMESTAMP_SEC:
		return StringUtil::Format("fromUnixTimestamp64Second(%d, 'UTC')", TimestampSValue::Get(val).value);
	case LogicalTypeId::TIMESTAMP_MS:
		return StringUtil::Format("fromUnixTimestamp64Milli(%d, 'UTC')", TimestampMSValue::Get(val).value);
	case LogicalTypeId::TIMESTAMP:
		return StringUtil::Format("fromUnixTimestamp64Micro(%d, 'UTC')", TimestampValue::Get(val).value);
	case LogicalTypeId::TIMESTAMP_NS:
		return StringUtil::Format("fromUnixTimestamp64Nano(%d, 'UTC')", TimestampNSValue::Get(val).value);
	case LogicalTypeId::TIME: {
		auto micros = TimeValue::Get(val).micros;
		auto seconds = micros / Interval::MICROS_PER_SEC;
		auto fraction = micros % Interval::MICROS_PER_SEC;
		auto decimal = StringUtil::Format("%d.%06d", seconds, fraction);
		return StringUtil::Format("toTime64(toDecimal64('%s', 6), 6)", decimal);
	}
	case LogicalTypeId::TIME_NS: {
		auto nanos = val.GetValueUnsafe<dtime_ns_t>().micros;
		auto seconds = nanos / Interval::NANOS_PER_SEC;
		auto fraction = nanos % Interval::NANOS_PER_SEC;
		auto decimal = StringUtil::Format("%d.%09d", seconds, fraction);
		return StringUtil::Format("toTime64(toDecimal64('%s', 9), 9)", decimal);
	}
	default:
		throw NotImplementedException("Unsupported constant type for filter pushdown");
	}
}

string ClickhouseFilterPushdown::TransformFilter(string &column_name, TableFilter &filter) {
	switch (filter.filter_type) {
	case TableFilterType::IS_NULL:
		return column_name + " IS NULL";
	case TableFilterType::IS_NOT_NULL:
		return column_name + " IS NOT NULL";
	case TableFilterType::CONJUNCTION_AND: {
		auto &conjunction_filter = filter.Cast<ConjunctionAndFilter>();
		return CreateExpression(column_name, conjunction_filter.child_filters, "AND");
	}
	case TableFilterType::CONJUNCTION_OR: {
		auto &conjunction_filter = filter.Cast<ConjunctionOrFilter>();
		return CreateExpression(column_name, conjunction_filter.child_filters, "OR");
	}
	case TableFilterType::CONSTANT_COMPARISON: {
		auto &constant_filter = filter.Cast<ConstantFilter>();
		auto constant_string = TransformConstant(constant_filter.constant);
		auto operator_string = TransformComparison(constant_filter.comparison_type);
		return StringUtil::Format("%s %s %s", column_name, operator_string, constant_string);
	}
	case TableFilterType::OPTIONAL_FILTER: {
		auto &optional_filter = filter.Cast<OptionalFilter>();
		return TransformFilter(column_name, *optional_filter.child_filter);
	}
	case TableFilterType::DYNAMIC_FILTER: {
		return string();
	}
	case TableFilterType::IN_FILTER: {
		auto &in_filter = filter.Cast<InFilter>();
		string in_list;
		for (auto &val : in_filter.values) {
			if (!in_list.empty()) {
				in_list += ", ";
			}
			in_list += TransformConstant(val);
		}
		auto column_expression = column_name;
		if (!in_filter.values.empty()) {
			switch (in_filter.values[0].type().id()) {
			case LogicalTypeId::TIME:
				column_expression = "CAST(" + column_name + " AS Time64(6))";
				break;
			case LogicalTypeId::TIME_NS:
				column_expression = "CAST(" + column_name + " AS Time64(9))";
				break;
			default:
				break;
			}
		}
		return column_expression + " IN (" + in_list + ")";
	}
	default:
		throw InternalException("Unsupported table filter type");
	}
}

string ClickhouseFilterPushdown::TransformFilters(const vector<column_t> &column_ids,
                                                  optional_ptr<TableFilterSet> filters, const vector<string> &names) {
	if (!filters || filters->filters.empty()) {
		// no filters
		return string();
	}
	string result;
	for (auto &entry : filters->filters) {
		auto column_name = ClickhouseUtils::WriteIdentifier(names[column_ids[entry.first]]);
		auto &filter = *entry.second;
		auto new_filter = TransformFilter(column_name, filter);
		if (new_filter.empty()) {
			continue;
		}
		if (!result.empty()) {
			result += " AND ";
		}
		result += new_filter;
	}
	return result;
}

} // namespace duckdb
