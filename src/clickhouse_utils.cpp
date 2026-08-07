#include "clickhouse_utils.hpp"

#include <duckdb.hpp>

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

static bool ParseValue(const string &dsn, idx_t &pos, string &result) {
	// skip leading spaces
	while (pos < dsn.size() && StringUtil::CharacterIsSpace(dsn[pos])) {
		pos++;
	}
	if (pos >= dsn.size()) {
		return false;
	}
	// check if we are parsing a quoted value or not
	if (dsn[pos] == '"') {
		pos++;
		// scan until we find another quote
		bool found_quote = false;
		for (; pos < dsn.size(); pos++) {
			if (dsn[pos] == '"') {
				found_quote = true;
				pos++;
				break;
			}
			if (dsn[pos] == '\\') {
				// backslash escapes the backslash or double-quote
				if (pos + 1 >= dsn.size()) {
					throw InvalidInputException("Invalid dsn \"%s\" - backslash at end of dsn", dsn);
				}
				if (dsn[pos + 1] != '\\' && dsn[pos + 1] != '"') {
					throw InvalidInputException("Invalid dsn \"%s\" - backslash can only escape \\ or \"", dsn);
				}
				result += dsn[pos + 1];
				pos++;
			} else {
				result += dsn[pos];
			}
		}
		if (!found_quote) {
			throw InvalidInputException("Invalid dsn \"%s\" - unterminated quote", dsn);
		}
	} else {
		// unquoted value, continue until space, equality sign or end of string
		for (; pos < dsn.size(); pos++) {
			if (dsn[pos] == '=') {
				break;
			}
			if (StringUtil::CharacterIsSpace(dsn[pos])) {
				break;
			}
			result += dsn[pos];
		}
	}
	return true;
}

// Implementation from https://github.com/duckdb/duckdb-mysql/blob/main/src/mysql_utils.cpp
clickhouse::ClientOptions ClickhouseUtils::ParseOptions(const string &attach_path) {
	auto options = clickhouse::ClientOptions();

	idx_t pos = 0;
	unordered_set<string> set_options;

	while (pos < attach_path.size()) {
		string key;
		string value;
		if (!ParseValue(attach_path, pos, key)) {
			break;
		}
		if (pos >= attach_path.size() || attach_path[pos] != '=') {
			throw InvalidInputException("Invalid dsn \"%s\" - expected key=value pairs separated by spaces",
			                            attach_path);
		}
		pos++;
		if (!ParseValue(attach_path, pos, value)) {
			throw InvalidInputException("Invalid dsn \"%s\" - expected key=value pairs separated by spaces",
			                            attach_path);
		}
		key = StringUtil::Lower(key);

		if (set_options.find(key) != set_options.end()) {
			throw InvalidInputException("Duplicate '%s' parameter in connection string. Each parameter "
			                            "should only be specified once.",
			                            key);
		}

		if (key == "host") {
			set_options.insert("host");
			options.SetHost(value);
		} else if (key == "port") {
			set_options.insert("port");
			options.SetPort(std::stoul(value));
		} else if (key == "database") {
			set_options.insert("database");
			options.SetDefaultDatabase(value);
		} else if (key == "user") {
			set_options.insert("user");
			options.SetUser(value);
		} else if (key == "password") {
			set_options.insert("password");
			options.SetPassword(value);
		}
	}

	return options;
}

} // namespace duckdb
