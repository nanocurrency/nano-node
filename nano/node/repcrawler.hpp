#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/node/transport/channel.hpp>
#include <celerix/node/transport/transport.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/optional.hpp>

#include <chrono>
#include <memory>
#include <thread>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace celerix
{
class node;
class active_elections;

struct representative
{
	celerix::account account;
	std::shared_ptr<celerix::transport::channel> channel;
};

class rep_crawler_config final
{
public:
	explicit rep_crawler_config (celerix::network_constants const &);

	celerix::error deserialize (celerix::tomlconfig & toml);
	celerix::error serialize (celerix::tomlconfig & toml) const;

public:
	std::chrono::milliseconds query_timeout{ 1000 * 60 };
};

/**
 * Crawls the network for representatives. Queries are performed by requesting confirmation of a
 * random block and observing the corresponding vote.
 */
class rep_crawler final
{
public:
	rep_crawler (rep_crawler_config const &, celerix::node &);
	~rep_crawler ();

	void start ();
	void stop ();

	/**
	 * Called when a non-replay vote arrives that might be of interest to rep crawler.
	 * @return true, if the vote was of interest and was processed, this indicates that the rep is likely online and voting
	 */
	bool process (std::shared_ptr<celerix::vote> const &, std::shared_ptr<celerix::transport::channel> const &);

	/** Attempt to determine if the peer manages one or more representative accounts */
	void query (std::deque<std::shared_ptr<celerix::transport::channel>> const & target_channels);

	/** Attempt to determine if the peer manages one or more representative accounts */
	void query (std::shared_ptr<celerix::transport::channel> const & target_channel);

	/** Query if a peer manages a principle representative */
	bool is_pr (std::shared_ptr<celerix::transport::channel> const &) const;

	/** Get total available weight from representatives */
	celerix::uint128_t total_weight () const;

	/** Request a list of the top \p count known representatives in descending order of weight, with at least \p weight_a voting weight, and optionally with a minimum version \p minimum_protocol_version */
	std::vector<representative> representatives (std::size_t count = std::numeric_limits<std::size_t>::max (), celerix::uint128_t minimum_weight = 0, std::optional<decltype (celerix::network_constants::protocol_version)> const & minimum_protocol_version = {}) const;

	/** Request a list of the top \p count known principal representatives in descending order of weight, optionally with a minimum version \p minimum_protocol_version */
	std::vector<representative> principal_representatives (std::size_t count = std::numeric_limits<std::size_t>::max (), std::optional<decltype (celerix::network_constants::protocol_version)> const & minimum_protocol_version = {}) const;

	/** Total number of representatives */
	std::size_t representative_count () const;

	celerix::container_info container_info () const;

private: // Dependencies
	rep_crawler_config const & config;
	celerix::node & node;
	celerix::stats & stats;
	celerix::logger & logger;
	celerix::network_constants & network_constants;
	celerix::active_elections & active;

private:
	void run ();
	void cleanup ();
	void validate_and_process (celerix::unique_lock<celerix::mutex> &);
	bool query_predicate (bool sufficient_weight) const;
	std::chrono::milliseconds query_interval (bool sufficient_weight) const;

	using hash_root_t = std::pair<celerix::block_hash, celerix::root>;

	/** Returns a list of endpoints to crawl. The total weight is passed in to avoid computing it twice. */

	std::deque<std::shared_ptr<celerix::transport::channel>> prepare_crawl_targets (bool sufficient_weight) const;
	std::optional<hash_root_t> prepare_query_target () const;
	bool track_rep_request (hash_root_t hash_root, std::shared_ptr<celerix::transport::channel> const & channel);

private:
	/**
	 * A representative picked up during repcrawl.
	 */
	struct rep_entry
	{
		rep_entry (celerix::account account_a, std::shared_ptr<celerix::transport::channel> const & channel_a) :
			account{ account_a },
			channel{ channel_a }
		{
			debug_assert (channel != nullptr);
		}

		celerix::account const account;
		std::shared_ptr<celerix::transport::channel> channel;

		std::chrono::steady_clock::time_point last_request{};
		std::chrono::steady_clock::time_point last_response{ std::chrono::steady_clock::now () };

		celerix::account get_account () const
		{
			return account;
		}
	};

	struct query_entry
	{
		celerix::block_hash hash;
		std::shared_ptr<celerix::transport::channel> channel;
		std::chrono::steady_clock::time_point time{ std::chrono::steady_clock::now () };
		unsigned int replies{ 0 }; // number of replies to the query
	};

	// clang-format off
	class tag_hash {};
	class tag_account {};
	class tag_channel {};
	class tag_sequenced {};

	using ordered_reps = boost::multi_index_container<rep_entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_account>,
			mi::const_mem_fun<rep_entry, celerix::account, &rep_entry::get_account>>,
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_non_unique<mi::tag<tag_channel>,
			mi::member<rep_entry, std::shared_ptr<celerix::transport::channel>, &rep_entry::channel>>
	>>;

	using ordered_queries = boost::multi_index_container<query_entry,
	mi::indexed_by<
		mi::hashed_non_unique<mi::tag<tag_channel>,
			mi::member<query_entry, std::shared_ptr<celerix::transport::channel>, &query_entry::channel>>,
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_non_unique<mi::tag<tag_hash>,
			mi::member<query_entry, celerix::block_hash, &query_entry::hash>>
	>>;
	// clang-format on

	ordered_reps reps;
	ordered_queries queries;

private:
	static size_t constexpr max_responses{ 1024 * 4 };

	using response_t = std::pair<std::shared_ptr<celerix::transport::channel>, std::shared_ptr<celerix::vote>>;
	boost::circular_buffer<response_t> responses{ max_responses };

	// Freshly established connections that should be queried asap
	std::deque<std::shared_ptr<celerix::transport::channel>> prioritized;

	std::chrono::steady_clock::time_point last_query{};

	std::atomic<bool> stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex;
	std::thread thread;

public: // Testing
	void force_add_rep (celerix::account const & account, std::shared_ptr<celerix::transport::channel> const & channel);
	void force_process (std::shared_ptr<celerix::vote> const & vote, std::shared_ptr<celerix::transport::channel> const & channel);
	void force_query (celerix::block_hash const & hash, std::shared_ptr<celerix::transport::channel> const & channel);
};
}
