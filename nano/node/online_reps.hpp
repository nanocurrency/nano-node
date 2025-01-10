#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/lib/numbers_templ.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <memory>
#include <thread>
#include <vector>

namespace mi = boost::multi_index;

namespace celerix
{
/** Track online representatives and trend online weight */
class online_reps final
{
public:
	online_reps (celerix::node_config const &, celerix::ledger &, celerix::stats &, celerix::logger &);
	~online_reps ();

	void start ();
	void stop ();

	/** Add voting account \p rep_account to the set of online representatives */
	void observe (celerix::account const & rep_account);

	/** Returns the trended online stake */
	celerix::uint128_t trended () const;
	/** Returns the current online stake */
	celerix::uint128_t online () const;
	/** Returns the quorum required for confirmation*/
	celerix::uint128_t delta () const;
	/** List of online representatives, both the currently sampling ones and the ones observed in the previous sampling period */
	std::vector<celerix::account> list ();
	void clear ();
	celerix::container_info container_info () const;

public:
	// TODO: This should be in the network constants
	static unsigned constexpr online_weight_quorum = 67;

private: // Dependencies
	celerix::node_config const & config;
	celerix::ledger & ledger;
	celerix::stats & stats;
	celerix::logger & logger;

private:
	void run ();
	/** Called periodically to sample online weight */
	void sample ();
	bool trim ();
	/** Remove old records from the database */
	void trim_trended (celerix::store::write_transaction const &);
	/** Iterate over all database samples and remove invalid records. This is meant to clean potential leftovers from previous versions. */
	void sanitize_trended (celerix::store::write_transaction const &);

	celerix::uint128_t calculate_trended (celerix::store::transaction const &) const;
	celerix::uint128_t calculate_online () const;

	bool verify_consistency (celerix::store::write_transaction const &, std::chrono::system_clock::time_point now, std::chrono::system_clock::time_point cutoff) const;

private:
	struct rep_info
	{
		std::chrono::steady_clock::time_point time;
		celerix::account account;
	};

	// clang-format off
	class tag_time {};
	class tag_account {};

	using ordered_reps = boost::multi_index_container<rep_info,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_time>,
			mi::member<rep_info, std::chrono::steady_clock::time_point, &rep_info::time>>,
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<rep_info, celerix::account, &rep_info::account>>
	>>;
	// clang-format off
	ordered_reps reps;

	celerix::uint128_t cached_trended{0};
	celerix::uint128_t cached_online{0};

	bool stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex;
	std::thread thread;

public: // Only for tests
	void force_online_weight (celerix::uint128_t const & online_weight);
	void force_sample ();
};
}
