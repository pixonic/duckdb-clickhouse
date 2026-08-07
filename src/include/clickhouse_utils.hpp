#pragma once

#include <clickhouse/client.h>

#include <string>

namespace duckdb {

class ClickhouseUtils {
public:
	static std::string WriteIdentifier(const std::string &identifier);
	static std::string WriteLiteral(const std::string &identifier);
	static std::string EscapeQuotes(const std::string &text, char quote);
	static std::string WriteQuoted(const std::string &text, char quote);
	static clickhouse::ClientOptions ParseOptions(const std::string &attach_path);
};

} // namespace duckdb
