#pragma once

#include <string>
#include <mutex>
#include <thread>

#include "duckdb/common/exception.hpp"

#include <clickhouse/client.h>
#include <msd/channel.hpp>

namespace duckdb {

struct ChannelEntry {
public:
	ChannelEntry(std::optional<clickhouse::Block> block, std::optional<IOException> error)
	    : block(std::move(block)), error(std::move(error)) {
	}

	ChannelEntry() {
	}

	static ChannelEntry FromBlock(const clickhouse::Block &block) {
		return ChannelEntry(std::optional(block), std::nullopt);
	}

	static ChannelEntry FromChError(const clickhouse::Exception &error) {
		return ChannelEntry(std::nullopt, std::optional(IOException(error.display_text)));
	}

	static ChannelEntry FromStdError(const std::exception &error) {
		return ChannelEntry(std::nullopt, std::optional(IOException(error.what())));
	}

public:
	std::optional<clickhouse::Block> block;
	std::optional<IOException> error;
};

using BlockChannel = msd::channel<ChannelEntry>;

class ClickhouseResult {
public:
	ClickhouseResult(std::shared_ptr<BlockChannel> channel, std::thread worker);
	~ClickhouseResult();

	ClickhouseResult(ClickhouseResult &&other) = default;
	ClickhouseResult &operator=(ClickhouseResult &&other) = delete;

	ClickhouseResult(const ClickhouseResult &other) = delete;
	ClickhouseResult &operator=(const ClickhouseResult &other) = delete;

	std::optional<clickhouse::Block> Next();

private:
	std::shared_ptr<BlockChannel> channel;
	std::thread worker;
};

class ClickhouseClient {
public:
	ClickhouseClient(const clickhouse::ClientOptions &opts, size_t channel_size);
	ClickhouseResult Query(const std::string &sql);

private:
	void ExecQuery(const std::string &sql, std::shared_ptr<BlockChannel> channel);

private:
	clickhouse::Client client;
	std::mutex lock;
	size_t channel_size;
};

} // namespace duckdb
