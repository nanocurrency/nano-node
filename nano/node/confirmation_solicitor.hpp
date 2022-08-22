#pragma once

#include <nano/node/network.hpp>
#include <nano/node/repcrawler.hpp>

#include <unordered_map>

namespace nano
{
class election;
class node;
class node_config;
class vote_info;

/** This class accepts elections that need further votes before they can be confirmed and bundles them in to single confirm_req packets */
class confirmation_solicitor final
{
public:
	confirmation_solicitor (nano::network &, nano::node_config const &);

	/*
	 * Prepare object for batching election confirmation requests
	 */
	void prepare (std::vector<nano::representative> const &);
	/*
	 * Broadcast the winner of an election if the broadcast limit has not been reached. Returns false if the broadcast was performed
	 */
	bool broadcast (std::shared_ptr<nano::block> const & winner, std::unordered_map<nano::account, nano::vote_info> const & last_votes);
	/*
	 * Add an election that needs to be confirmed. Returns false if successfully added
	 */
	bool add (std::shared_ptr<nano::block> const & winner, std::unordered_map<nano::account, nano::vote_info> const & last_votes);
	/** Dispatch bundled requests to each channel*/
	void flush ();

	/** Global maximum amount of block broadcasts */
	std::size_t const max_block_broadcasts;
	/** Maximum amount of requests to be sent per election, bypassed if an existing vote is for a different hash*/
	std::size_t const max_election_requests;
	/** Maximum amount of directed broadcasts to be sent per election */
	std::size_t const max_election_broadcasts;

private:
	nano::network & network;
	nano::node_config const & config;

	unsigned rebroadcasted{ 0 };
	std::vector<nano::representative> representatives_requests;
	std::vector<nano::representative> representatives_broadcasts;
	using vector_root_hashes = std::vector<std::pair<nano::block_hash, nano::root>>;
	std::unordered_map<std::shared_ptr<nano::transport::channel>, vector_root_hashes> requests;
	bool prepared{ false };
};
}
