#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <memory>
#include <thread>
#include <vector>

namespace mi = boost::multi_index;

namespace nano
{
/** Track online representatives and trend online weight */
class online_reps final
{
public:
	online_reps (nano::node_config const &, nano::ledger &, nano::stats &, nano::logger &);
	~online_reps ();

	void start ();
	void stop ();

	/** Add voting account \p rep_account to the set of online representatives */
	void observe (nano::account const & rep_account);

	/** Returns the trended online stake */
	nano::uint128_t trended () const;
	/** Returns the current online stake */
	nano::uint128_t online () const;
	/** Returns the quorum required for confirmation*/
	nano::uint128_t delta () const;
	/** List of online representatives, both the currently sampling ones and the ones observed in the previous sampling period */
	std::vector<nano::account> list ();
	void clear ();

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name);

	// TODO: This should be in network constants
	static unsigned constexpr online_weight_quorum = 67;

private: // Dependencies
	nano::node_config const & config;
	nano::ledger & ledger;
	nano::stats & stats;
	nano::logger & logger;

private:
	void run ();
	/** Called periodically to sample online weight */
	void sample ();
	bool trim ();
	/** Remove old records from the database */
	void trim_trend (nano::store::write_transaction const &);
	/** Iterate over all database samples and remove invalid records. This is meant to clean potential leftovers from previous versions. */
	void sanitize_trend (nano::store::write_transaction const &);
	nano::uint128_t calculate_trend (nano::store::transaction const &) const;
	nano::uint128_t calculate_online () const;

private:
	struct rep_info
	{
		std::chrono::steady_clock::time_point time;
		nano::account account;
	};

	// clang-format off
	class tag_time {};
	class tag_account {};

	using ordered_reps = boost::multi_index_container<rep_info,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_time>,
			mi::member<rep_info, std::chrono::steady_clock::time_point, &rep_info::time>>,
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<rep_info, nano::account, &rep_info::account>>
	>>;
	// clang-format off
	ordered_reps reps;

	nano::uint128_t trended_m;
	nano::uint128_t online_m;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;

public: // Only for tests
	void force_online_weight (nano::uint128_t const & online_weight);
	void force_sample ();
};
}
