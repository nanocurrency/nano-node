#pragma once

#include <nano/node/common.hpp>

#include <future>

namespace nano
{
class bootstrap_attempt_legacy;
class bootstrap_client;

/**
 * Client side of a bulk_push request. Sends a sequence of blocks the other side did not report in their frontier_req response.
 */
class bulk_push_client final : public std::enable_shared_from_this<nano::bulk_push_client>
{
public:
	explicit bulk_push_client (std::shared_ptr<nano::bootstrap_client> const &, std::shared_ptr<nano::bootstrap_attempt_legacy> const &);
	~bulk_push_client ();
	void start ();
	void push ();
	void push_block (nano::block const &);
	void send_finished ();
	std::shared_ptr<nano::bootstrap_client> connection;
	std::shared_ptr<nano::bootstrap_attempt_legacy> attempt;
	std::promise<bool> promise;
	std::pair<nano::block_hash, nano::block_hash> current_target;
};
class bootstrap_server;

/**
 * Server side of a bulk_push request. Receives blocks and puts them in the block processor to be processed.
 */
class bulk_push_server final : public std::enable_shared_from_this<nano::bulk_push_server>
{
public:
	explicit bulk_push_server (std::shared_ptr<nano::bootstrap_server> const &);
	void throttled_receive ();
	void receive ();
	void received_type ();
	void received_block (boost::system::error_code const &, std::size_t, nano::block_type);
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::shared_ptr<nano::bootstrap_server> connection;
};
}
