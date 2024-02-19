#pragma once

#include <nano/lib/locks.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/optional.hpp>

#include <chrono>
#include <memory>
#include <thread>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace nano
{
class node;
class active_transactions;

struct representative
{
	nano::account account;
	std::shared_ptr<nano::transport::channel> channel;
};

/**
 * Crawls the network for representatives. Queries are performed by requesting confirmation of a
 * random block and observing the corresponding vote.
 */
class rep_crawler final
{
	friend std::unique_ptr<container_info_component> collect_container_info (rep_crawler & rep_crawler, std::string const & name);

public:
	explicit rep_crawler (nano::node &);
	~rep_crawler ();

	void start ();
	void stop ();

	/**
	 * Called when a non-replay vote on a block previously sent by query() is received. This indicates
	 * with high probability that the endpoint is a representative node.
	 * The force flag can be set to skip the active check in unit testing when we want to force a vote in the rep crawler.
	 * @return false if any vote passed the checks and was added to the response queue of the rep crawler
	 */
	bool process (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> const &);

	/** Remove block hash from list of active rep queries */
	void remove (nano::block_hash const &);

	/** Remove block hash from with delay depending on vote processor size */
	void throttled_remove (nano::block_hash const &, uint64_t target_finished_processed);

	/** Attempt to determine if the peer manages one or more representative accounts */
	void query (std::vector<std::shared_ptr<nano::transport::channel>> const & target_channels);

	/** Attempt to determine if the peer manages one or more representative accounts */
	void query (std::shared_ptr<nano::transport::channel> const & target_channel);

	/** Query if a peer manages a principle representative */
	bool is_pr (std::shared_ptr<nano::transport::channel> const &) const;

	/** Get total available weight from representatives */
	nano::uint128_t total_weight () const;

	/** Request a list of the top \p count_a known representatives in descending order of weight, with at least \p weight_a voting weight, and optionally with a minimum version \p opt_version_min_a */
	std::vector<representative> representatives (std::size_t count_a = std::numeric_limits<std::size_t>::max (), nano::uint128_t weight_a = 0, boost::optional<decltype (nano::network_constants::protocol_version)> const & opt_version_min_a = boost::none);

	/** Request a list of the top \p count_a known principal representatives in descending order of weight, optionally with a minimum version \p opt_version_min_a */
	std::vector<representative> principal_representatives (std::size_t count_a = std::numeric_limits<std::size_t>::max (), boost::optional<decltype (nano::network_constants::protocol_version)> const & opt_version_min_a = boost::none);

	/** Total number of representatives */
	std::size_t representative_count ();

private: // Dependencies
	nano::node & node;
	nano::stats & stats;
	nano::network_constants & network_constants;
	nano::active_transactions & active;

private:
	void run ();
	void cleanup ();
	void validate_and_process (nano::unique_lock<nano::mutex> &);

	/** Returns a list of endpoints to crawl. The total weight is passed in to avoid computing it twice. */
	std::vector<std::shared_ptr<nano::transport::channel>> get_crawl_targets (nano::uint128_t current_total_weight) const;

	/** When a rep request is made, this is called to update the last-request timestamp. */
	void on_rep_request (std::shared_ptr<nano::transport::channel> const & channel_a);

	std::optional<std::pair<nano::block_hash, nano::block_hash>> prepare_query_target ();

private:
	/**
	 * A representative picked up during repcrawl.
	 */
	struct representative_entry
	{
		representative_entry (nano::account account_a, std::shared_ptr<nano::transport::channel> const & channel_a) :
			account{ account_a },
			channel{ channel_a }
		{
			debug_assert (channel != nullptr);
		}

		nano::account const account;
		std::shared_ptr<nano::transport::channel> channel;

		std::chrono::steady_clock::time_point last_request{};
		std::chrono::steady_clock::time_point last_response{};

		nano::account get_account () const
		{
			return account;
		}
	};

	// clang-format off
	class tag_account {};
	class tag_channel {};
	class tag_sequenced {};

	using ordered_reps = boost::multi_index_container<representative_entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_account>,
			mi::const_mem_fun<representative_entry, nano::account, &representative_entry::get_account>>,
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_non_unique<mi::tag<tag_channel>,
			mi::member<representative_entry, std::shared_ptr<nano::transport::channel>, &representative_entry::channel>>
	>>;
	// clang-format on

	ordered_reps reps;

private:
	/** We have solicited votes for these random blocks */
	std::unordered_set<nano::block_hash> queries;

	std::deque<std::pair<std::shared_ptr<nano::transport::channel>, std::shared_ptr<nano::vote>>> responses;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;

public: // Testing
	void force_add_rep (nano::account const & account, std::shared_ptr<nano::transport::channel> const & channel);
	void force_process (std::shared_ptr<nano::vote> const & vote, std::shared_ptr<nano::transport::channel> const & channel);
	void force_active_query (nano::block_hash const & hash);
};

std::unique_ptr<container_info_component> collect_container_info (rep_crawler & rep_crawler, std::string const & name);
}
