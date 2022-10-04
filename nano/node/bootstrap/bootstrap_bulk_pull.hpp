#pragma once

#include <nano/node/common.hpp>
#include <nano/node/messages.hpp>
#include <nano/node/socket.hpp>

#include <unordered_set>

namespace nano
{
class bootstrap_attempt;
namespace bootstrap
{
	class block_deserializer;
};
class pull_info
{
public:
	using count_t = nano::bulk_pull::count_t;
	pull_info () = default;
	pull_info (nano::hash_or_account const &, nano::block_hash const &, nano::block_hash const &, uint64_t, count_t = 0, unsigned = 16);
	nano::hash_or_account account_or_head{ 0 };
	nano::block_hash head{ 0 };
	nano::block_hash head_original{ 0 };
	nano::block_hash end{ 0 };
	count_t count{ 0 };
	unsigned attempts{ 0 };
	uint64_t processed{ 0 };
	unsigned retry_limit{ 0 };
	uint64_t bootstrap_id{ 0 };
};
class bootstrap_client;

/**
 * Client side of a bulk_pull request. Created when the bootstrap_attempt wants to make a bulk_pull request to the remote side.
 */
class bulk_pull_client final : public std::enable_shared_from_this<nano::bulk_pull_client>
{
public:
	bulk_pull_client (std::shared_ptr<nano::bootstrap_client> const &, std::shared_ptr<nano::bootstrap_attempt> const &, nano::pull_info const &);
	~bulk_pull_client ();
	void request ();
	void receive_block ();
	void throttled_receive_block ();
	void received_block (boost::system::error_code ec, std::shared_ptr<nano::block> block);
	nano::block_hash first ();
	std::shared_ptr<nano::bootstrap_client> connection;
	std::shared_ptr<nano::bootstrap_attempt> attempt;
	bool network_error{ false };

private:
	/**
	 * Tracks the next block expected to be received starting with the block hash that was expected and followed by previous blocks for this account chain
	 */
	nano::block_hash expected{ 0 };
	/**
	 * Tracks the account number for this account chain
	 * Used when an account chain has a mix between state blocks and legacy blocks which do not encode the account number in the block
	 * 0 if the account is unknown
	 */
	nano::account known_account{ 0 };
	/**
	 * Original pull request
	 */
	nano::pull_info pull;
	/**
	 * Tracks the number of blocks successfully deserialized
	 */
	uint64_t pull_blocks{ 0 };
	/**
	 * Tracks the number of times an unexpected block was received
	 */
	uint64_t unexpected_count{ 0 };
	std::shared_ptr<nano::bootstrap::block_deserializer> block_deserializer;
};
class bootstrap_attempt_wallet;
class bulk_pull_account_client final : public std::enable_shared_from_this<nano::bulk_pull_account_client>
{
public:
	bulk_pull_account_client (std::shared_ptr<nano::bootstrap_client> const &, std::shared_ptr<nano::bootstrap_attempt_wallet> const &, nano::account const &);
	~bulk_pull_account_client ();
	void request ();
	void receive_pending ();
	std::shared_ptr<nano::bootstrap_client> connection;
	std::shared_ptr<nano::bootstrap_attempt_wallet> attempt;
	nano::account account;
	uint64_t pull_blocks;
};
class bootstrap_server;
class bulk_pull;

/**
 * Server side of a bulk_pull request. Created when bootstrap_server receives a bulk_pull message and is exited after the contents
 * have been sent. If the 'start' in the bulk_pull message is an account, send blocks for that account down to 'end'. If the 'start'
 * is a block hash, send blocks for that chain down to 'end'. If end doesn't exist, send all accounts in the chain.
 */
class bulk_pull_server final : public std::enable_shared_from_this<nano::bulk_pull_server>
{
public:
	bulk_pull_server (std::shared_ptr<nano::bootstrap_server> const &, std::unique_ptr<nano::bulk_pull>);
	void set_current_end ();
	std::shared_ptr<nano::block> get_next ();
	void send_next ();
	void sent_action (boost::system::error_code const &, std::size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, std::size_t);
	bool ascending () const;
	std::shared_ptr<nano::bootstrap_server> connection;
	std::unique_ptr<nano::bulk_pull> request;
	nano::block_hash current;
	bool include_start;
	nano::bulk_pull::count_t max_count;
	nano::bulk_pull::count_t sent_count;
};
class bulk_pull_account;
class bulk_pull_account_server final : public std::enable_shared_from_this<nano::bulk_pull_account_server>
{
public:
	bulk_pull_account_server (std::shared_ptr<nano::bootstrap_server> const &, std::unique_ptr<nano::bulk_pull_account>);
	void set_params ();
	std::pair<std::unique_ptr<nano::pending_key>, std::unique_ptr<nano::pending_info>> get_next ();
	void send_frontier ();
	void send_next_block ();
	void sent_action (boost::system::error_code const &, std::size_t);
	void send_finished ();
	void complete (boost::system::error_code const &, std::size_t);
	std::shared_ptr<nano::bootstrap_server> connection;
	std::unique_ptr<nano::bulk_pull_account> request;
	std::unordered_set<nano::uint256_union> deduplication;
	nano::pending_key current_key;
	bool pending_address_only;
	bool pending_include_address;
	bool invalid_request;
};
}
