#include "clickhouse_client.hpp"

#include <thread>

#include "duckdb/common/printer.hpp"

namespace duckdb {

ClickhouseResult::ClickhouseResult(std::shared_ptr<BlockChannel> channel, std::thread worker)
    : channel(std::move(channel)), worker(std::move(worker)) {
}

ClickhouseResult::~ClickhouseResult() {
	if (channel) {
		channel->close();
	}
	if (worker.joinable()) {
		worker.join();
	}
}

std::optional<clickhouse::Block> ClickhouseResult::Next() {
	ChannelEntry entry;
	auto succeed = channel->read(entry);
	if (!succeed) {
		return std::nullopt;
	}
	if (entry.error.has_value()) {
		throw std::move(entry.error.value());
	}
	return entry.block;
}

ClickhouseClient::ClickhouseClient(const clickhouse::ClientOptions &opts, size_t channel_size)
    : client(opts), channel_size(channel_size) {
}

ClickhouseResult ClickhouseClient::Query(const std::string &sql) {
	// Printer::Print(sql);
	auto channel = std::make_shared<BlockChannel>(channel_size);
	std::thread t(&ClickhouseClient::ExecQuery, this, sql, channel);
	return ClickhouseResult(channel, std::move(t));
}

void ClickhouseClient::ExecQuery(const std::string &sql, std::shared_ptr<BlockChannel> channel) {
	std::lock_guard<std::mutex> l(lock);

	auto query = clickhouse::Query(sql);
	query.OnDataCancelable(
	    [=](const clickhouse::Block &block) { return channel->write(ChannelEntry::FromBlock(block)); });
	query.OnException([=](const clickhouse::Exception &ex) { channel->write(ChannelEntry::FromChError(ex)); });
	try {
		client.Select(query);
	} catch (const std::exception &error) {
		channel->write(ChannelEntry::FromStdError(error));
	}
	channel->close();
}

} // namespace duckdb
