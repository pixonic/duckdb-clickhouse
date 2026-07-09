#pragma once

#include <string>
#include <mutex>

#include <clickhouse/client.h>
#include <msd/channel.hpp>

namespace duckdb {

struct ChannelEntry {
public:
	ChannelEntry(std::optional<clickhouse::Block> block, std::optional<std::runtime_error> error)
	    : block(std::move(block)), error(std::move(error)) {
	}

	ChannelEntry() {
	}

	static ChannelEntry FromBlock(const clickhouse::Block &block) {
		return ChannelEntry(std::optional(block), std::nullopt);
	}

	static ChannelEntry FromError(const clickhouse::Exception &error) {
		// TODO fix
		return ChannelEntry(std::nullopt, std::optional(std::runtime_error(error.display_text)));
	}

public:
	std::optional<clickhouse::Block> block;
	std::optional<std::runtime_error> error;
};

using BlockChannel = msd::channel<ChannelEntry>;

class ClickhouseResult {
public:
	explicit ClickhouseResult(std::shared_ptr<BlockChannel> channel);
	std::optional<clickhouse::Block> Next();

private:
	std::shared_ptr<BlockChannel> channel;
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
