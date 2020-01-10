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
	bootstrap_client (std::shared_ptr<nano::node> node_a, std::shared_ptr<nano::bootstrap_attempt> attempt_a, std::shared_ptr<nano::transport::channel_tcp> channel_a, std::shared_ptr<nano::socket> socket_a);
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

class bootstrap_connections final : public std::enable_shared_from_this<bootstrap_connections>
{
public:
	bootstrap_connections (std::shared_ptr<nano::node> node_a);
	std::shared_ptr<nano::bootstrap_connections> shared ();
	std::shared_ptr<nano::bootstrap_client> connection (nano::unique_lock<std::mutex> & lock_a, bool use_front_connection = false);
	void pool_connection (std::shared_ptr<nano::bootstrap_client> client_a);
	void add_connection (nano::endpoint const &);
	void connect_client (nano::tcp_endpoint const &);
	unsigned target_connections (size_t pulls_remaining);
	void populate_connections ();
	void start_populate_connections ();
	void stop ();
	
	std::deque<std::weak_ptr<nano::bootstrap_client>> clients;
	std::atomic<unsigned> connections_count{ 0 };
	std::shared_ptr<nano::node> node;
	std::shared_ptr<nano::bootstrap_attempt> attempt;
	std::deque<std::shared_ptr<nano::bootstrap_client>> idle;
	std::atomic<bool> populate_connections_started{ false };
	std::atomic<bool> stopped{ false };
	std::mutex mutex;
	nano::condition_variable condition;
};
}
