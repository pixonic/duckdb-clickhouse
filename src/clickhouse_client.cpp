#include "clickhouse_client.hpp"

#include <thread>

#include "duckdb/common/printer.hpp"

#include <clickhouse/base/socket.h>

namespace duckdb {

namespace {

class ClickhouseTimedSocketInput : public clickhouse::SocketInput {
public:
	ClickhouseTimedSocketInput(SOCKET socket, std::chrono::steady_clock::duration &read_duration)
	    : clickhouse::SocketInput(socket), read_duration(read_duration) {
	}

protected:
	size_t DoRead(void *buffer, size_t size) override {
		auto start = std::chrono::steady_clock::now();
		try {
			auto result = clickhouse::SocketInput::DoRead(buffer, size);
			read_duration += std::chrono::steady_clock::now() - start;
			return result;
		} catch (...) {
			read_duration += std::chrono::steady_clock::now() - start;
			throw;
		}
	}

private:
	std::chrono::steady_clock::duration &read_duration;
};

class ClickhouseTimedSocket : public clickhouse::Socket {
public:
	ClickhouseTimedSocket(const clickhouse::NetworkAddress &address, const clickhouse::SocketTimeoutParams &timeouts,
	                      std::chrono::steady_clock::duration &read_duration)
	    : clickhouse::Socket(address, timeouts), read_duration(read_duration) {
	}

	std::unique_ptr<clickhouse::InputStream> makeInputStream() const override {
		return std::make_unique<ClickhouseTimedSocketInput>(handle_, read_duration);
	}

private:
	std::chrono::steady_clock::duration &read_duration;
};

class ClickhouseTimedSocketFactory : public clickhouse::NonSecureSocketFactory {
public:
	explicit ClickhouseTimedSocketFactory(std::chrono::steady_clock::duration &read_duration)
	    : read_duration(read_duration) {
	}

protected:
	std::unique_ptr<clickhouse::Socket> doConnect(const clickhouse::NetworkAddress &address,
	                                              const clickhouse::ClientOptions &opts) override {
		clickhouse::SocketTimeoutParams timeouts {opts.connection_connect_timeout, opts.connection_recv_timeout,
		                                          opts.connection_send_timeout};
		return std::make_unique<ClickhouseTimedSocket>(address, timeouts, read_duration);
	}

private:
	std::chrono::steady_clock::duration &read_duration;
};

} // namespace

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
    : client(opts, std::make_unique<ClickhouseTimedSocketFactory>(socket_read_duration)), channel_size(channel_size) {
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
	// Reset after connection setup, and while holding the client lock, to measure only this query's reads.
	socket_read_duration = std::chrono::steady_clock::duration::zero();
	try {
		client.Select(query);
	} catch (const std::exception &error) {
		channel->write(ChannelEntry::FromStdError(error));
	}
	channel->close();

	// Only SocketInput::DoRead is timed. Blocking reads still include waiting for the server to produce data.
	auto receive_ms = std::chrono::duration<double, std::milli>(socket_read_duration).count();
	Printer::Print(
	    OutputStream::STREAM_STDOUT,
	    StringUtil::Format("ClickHouse socket receive time: %.3f ms (includes waiting for server data)", receive_ms));
}

} // namespace duckdb
