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
	/** Add an election that needs to be confirmed. Returns false if a request will be performed on flush() */
	bool add (std::shared_ptr<nano::election>);
	/** Dispatch bundled requests to each channel*/
	void flush ();
	/** The maximum amount of confirmation requests (batches) to be sent to each channel */
	size_t const max_confirm_req_batches;
	size_t const max_block_broadcasts;

private:
	nano::network & network;

	std::chrono::milliseconds const min_time_between_requests;
	std::chrono::milliseconds const min_time_between_floods;
	size_t const min_request_count_flood;
	int rebroadcasted{ 0 };
	std::vector<nano::representative> representatives;
	using vector_root_hashes = std::vector<std::pair<nano::block_hash, nano::root>>;
	std::unordered_map<std::shared_ptr<nano::transport::channel>, vector_root_hashes> requests;
	bool prepared{ false };
};
}
