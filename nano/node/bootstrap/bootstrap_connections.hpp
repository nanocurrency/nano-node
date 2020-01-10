#pragma once

#include <nano/node/common.hpp>
#include <nano/node/socket.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/ledger.hpp>


#include <atomic>
#include <future>

namespace nano
{
class node;
namespace transport
{
	class channel_tcp;
}

class bootstrap_attempt;
class frontier_req_client;
class bulk_push_client;
class pull_info;
class bootstrap_client final : public std::enable_shared_from_this<bootstrap_client>
{
public:
	bootstrap_client (std::shared_ptr<nano::node>, std::shared_ptr<nano::bootstrap_attempt>, std::shared_ptr<nano::transport::channel_tcp>, std::shared_ptr<nano::socket>);
	~bootstrap_client ();
	std::shared_ptr<nano::bootstrap_client> shared ();
	void stop (bool force);
	double block_rate () const;
	double elapsed_seconds () const;
	std::shared_ptr<nano::node> node;
	std::shared_ptr<nano::bootstrap_attempt> attempt;
	std::shared_ptr<nano::transport::channel_tcp> channel;
	std::shared_ptr<nano::socket> socket;
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::chrono::steady_clock::time_point start_time;
	std::atomic<uint64_t> block_count{ 0 };
	std::atomic<bool> pending_stop{ false };
	std::atomic<bool> hard_stop{ false };
};
}
