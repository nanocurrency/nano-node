#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/fair_queue.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/vote.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <thread>
#include <unordered_map>

namespace mi = boost::multi_index;

namespace nano
{
class vote_rebroadcaster_config final
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	bool enable{ true };
	size_t max_queue{ 1024 * 4 }; // Maximum number of votes to keep in queue for processing
	size_t max_history{ 1024 * 32 }; // Maximum number of recently broadcast hashes to keep per representative
	size_t max_representatives{ 100 }; // Maximum number of representatives to track rebroadcasts for
	std::chrono::milliseconds rebroadcast_threshold{ 1000 * 90 }; // Minimum amount of time between rebroadcasts for the same hash from the same representative (milliseconds)
	size_t priority_coefficient{ 2 }; // Priority coefficient for prioritizing votes from representative tiers
};

class vote_rebroadcaster_index
{
public:
	explicit vote_rebroadcaster_index (vote_rebroadcaster_config const &);

	enum class result
	{
		ok,
		already_rebroadcasted,
		representatives_full,
		rebroadcast_unnecessary,
	};

	result check_and_record (std::shared_ptr<nano::vote> const & vote, nano::uint128_t rep_weight, std::chrono::steady_clock::time_point now);

	using rep_query = std::function<std::pair<bool, nano::uint128_t> (nano::account const &)>; // Returns <should keep, rep weight>
	size_t cleanup (rep_query);

	bool contains_vote (nano::block_hash const & vote_hash) const;
	bool contains_representative (nano::account const & representative) const;
	bool contains_block (nano::account const & representative, nano::block_hash const & block_hash) const;

	size_t representatives_count () const;
	size_t total_history () const;
	size_t total_hashes () const;

private:
	vote_rebroadcaster_config const & config;

	struct rebroadcast_entry
	{
		nano::block_hash block_hash;
		nano::vote_timestamp vote_timestamp;
		std::chrono::steady_clock::time_point timestamp;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_vote_hash {};
	class tag_block_hash {};

	// Tracks rebroadcast history for individual block hashes
	using ordered_rebroadcasts = boost::multi_index_container<rebroadcast_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_block_hash>,
			mi::member<rebroadcast_entry, nano::block_hash, &rebroadcast_entry::block_hash>>
	>>;

	// Tracks rebroadcast history for full votes
	using ordered_hashes = boost::multi_index_container<nano::block_hash,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_vote_hash>,
			mi::identity<nano::block_hash>>
	>>;
	// clang-format on

	struct representative_entry
	{
		nano::account representative;
		nano::uint128_t weight;

		mutable ordered_rebroadcasts history;
		mutable ordered_hashes hashes;
	};

	// clang-format off
	class tag_account {};
	class tag_weight {};

	using ordered_representatives = boost::multi_index_container<representative_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<representative_entry, nano::account, &representative_entry::representative>>,
		mi::ordered_non_unique<mi::tag<tag_weight>,
			mi::member<representative_entry, nano::uint128_t, &representative_entry::weight>>
	>>;
	// clang-format on

	ordered_representatives index;
};

class vote_rebroadcaster final
{
public:
	vote_rebroadcaster (vote_rebroadcaster_config const &, nano::ledger &, nano::vote_router &, nano::network &, nano::wallets &, nano::rep_tiers &, nano::stats &, nano::logger &);
	~vote_rebroadcaster ();

	void start ();
	void stop ();

	bool push (std::shared_ptr<nano::vote> const &, nano::rep_tier);

	nano::container_info container_info () const;

public: // Dependencies
	vote_rebroadcaster_config const & config;
	nano::ledger & ledger;
	nano::vote_router & vote_router;
	nano::network & network;
	nano::wallets & wallets;
	nano::rep_tiers & rep_tiers;
	nano::stats & stats;
	nano::logger & logger;

private:
	void run ();
	void cleanup ();
	bool process (std::shared_ptr<nano::vote> const &);
	std::pair<std::shared_ptr<nano::vote>, nano::rep_tier> next ();

private:
	// Queue of recently processed votes to potentially rebroadcast
	nano::fair_queue<std::shared_ptr<nano::vote>, nano::rep_tier> queue;
	std::unordered_set<nano::signature> queue_hashes; // Avoids queuing the same vote multiple times

	nano::locked<vote_rebroadcaster_index> rebroadcasts;

private:
	std::atomic<bool> non_principal{ true };
	nano::wallet_representatives reps;
	nano::interval refresh_interval;
	nano::interval cleanup_interval;

	bool stopped{ false };
	std::condition_variable condition;
	mutable std::mutex mutex;
	std::thread thread;

	nano::interval log_interval;
};
}