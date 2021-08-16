#pragma once

#include "nano/lib/blocks.hpp" // for block (ptr only), block_type
#include "nano/lib/numbers.hpp" // for block_hash

#include <future> // for promise
#include <utility> // for pair
#include <vector> // for vector

#include <bits/shared_ptr.h> // for shared_ptr, enable_shared_from_this
#include <bits/stdint-uintn.h> // for uint8_t
#include <stddef.h> // for size_t

namespace boost
{
namespace system
{
	class error_code;
}
}

namespace nano
{
class bootstrap_attempt;
class bootstrap_client;
class bulk_push_client final : public std::enable_shared_from_this<nano::bulk_push_client>
{
public:
	explicit bulk_push_client (std::shared_ptr<nano::bootstrap_client> const &, std::shared_ptr<nano::bootstrap_attempt> const &);
	~bulk_push_client ();
	void start ();
	void push ();
	void push_block (nano::block const &);
	void send_finished ();
	std::shared_ptr<nano::bootstrap_client> connection;
	std::shared_ptr<nano::bootstrap_attempt> attempt;
	std::promise<bool> promise;
	std::pair<nano::block_hash, nano::block_hash> current_target;
};
class bootstrap_server;
class bulk_push_server final : public std::enable_shared_from_this<nano::bulk_push_server>
{
public:
	explicit bulk_push_server (std::shared_ptr<nano::bootstrap_server> const &);
	void throttled_receive ();
	void receive ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t, nano::block_type);
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::shared_ptr<nano::bootstrap_server> connection;
};
}
