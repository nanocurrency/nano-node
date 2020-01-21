#pragma once

#include <nano/node/network.hpp>
#include <nano/node/repcrawler.hpp>

#include <unordered_map>

namespace nano
{
class election;
class node;
/** This class accepts elections that need further votes before they can be confirmed and bundles them in to single confirm_req packets */
class confirmation_solicitor final
{
public:
	confirmation_solicitor (nano::network &, nano::network_constants const &);
	/** Prepare object for batching election confirmation requests*/
	void prepare (std::vector<nano::representative> const &);
	/** Broadcast the winner of an election if the broadcast limit has not been reached. Returns false if the broadcast was performed */
	bool broadcast (nano::election const &);
	/** Add an election that needs to be confirmed. Returns false if successfully added */
	bool add (nano::election const &);
	/** Dispatch bundled requests to each channel*/
	void flush ();
	/** The maximum amount of confirmation requests (batches) to be sent to each channel */
	size_t const max_confirm_req_batches;
	/** The global maximum amount of block broadcasts */
	size_t const max_block_broadcasts;

private:
	nano::network & network;

	int rebroadcasted{ 0 };
	std::vector<nano::representative> representatives;
	using vector_root_hashes = std::vector<std::pair<nano::block_hash, nano::root>>;
	std::unordered_map<std::shared_ptr<nano::transport::channel>, vector_root_hashes> requests;
	bool prepared{ false };
};
}
