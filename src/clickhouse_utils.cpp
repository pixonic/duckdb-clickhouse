#include "clickhouse_utils.hpp"

using namespace std;

namespace duckdb {

string ClickhouseUtils::EscapeQuotes(const string &text, char quote) {
	string result;
	for (auto c : text) {
		if (c == quote) {
			result += "\\";
			result += quote;
		} else if (c == '\\') {
			result += "\\\\";
		} else {
			result += c;
		}
	}
	return result;
}

string ClickhouseUtils::WriteQuoted(const string &text, char quote) {
	return string(1, quote) + EscapeQuotes(text, quote) + string(1, quote);
}

string ClickhouseUtils::WriteIdentifier(const string &identifier) {
	return ClickhouseUtils::WriteQuoted(identifier, '`');
}

string ClickhouseUtils::WriteLiteral(const string &identifier) {
	return ClickhouseUtils::WriteQuoted(identifier, '\'');
}

} // namespace duckdb
