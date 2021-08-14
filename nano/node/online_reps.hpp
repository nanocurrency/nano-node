#pragma once

#include <boost/multi_index/hashed_index.hpp>   // for hashed_unique
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>  // for ordered_non_unique
#include <boost/multi_index/indexed_by.hpp>     // for indexed_by
#include <boost/multi_index/tag.hpp>            // for tag
#include <boost/multi_index_container.hpp>      // for multi_index_container
#include <chrono>                               // for steady_clock, steady_...
#include <memory>                               // for unique_ptr
#include <nano/lib/numbers.hpp>                 // for uint128_t, account
#include <string>                               // for string
#include <vector>                               // for vector
#include "nano/lib/locks.hpp"                   // for mutex

namespace boost { namespace multi_index { template <class Class, typename Type, Type Class::*PtrToMember> struct member; } }
namespace nano { class container_info_component; }

namespace nano
{
class ledger;
class node_config;
class transaction;

/** Track online representatives and trend online weight */
class online_reps final
{
public:
	online_reps (nano::ledger & ledger_a, nano::node_config const & config_a);
	/** Add voting account \p rep_account to the set of online representatives */
	void observe (nano::account const & rep_account);
	/** Called periodically to sample online weight */
	void sample ();
	/** Returns the trended online stake */
	nano::uint128_t trended () const;
	/** Returns the current online stake */
	nano::uint128_t online () const;
	/** Returns the quorum required for confirmation*/
	nano::uint128_t delta () const;
	/** List of online representatives, both the currently sampling ones and the ones observed in the previous sampling period */
	std::vector<nano::account> list ();
	void clear ();
	static unsigned constexpr online_weight_quorum = 67;

private:
	class rep_info
	{
	public:
		std::chrono::steady_clock::time_point time;
		nano::account account;
	};
	class tag_time
	{
	};
	class tag_account
	{
	};
	nano::uint128_t calculate_trend (nano::transaction &) const;
	nano::uint128_t calculate_online () const;
	mutable nano::mutex mutex;
	nano::ledger & ledger;
	nano::node_config const & config;
	boost::multi_index_container<rep_info,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::tag<tag_time>,
	boost::multi_index::member<rep_info, std::chrono::steady_clock::time_point, &rep_info::time>>,
	boost::multi_index::hashed_unique<boost::multi_index::tag<tag_account>,
	boost::multi_index::member<rep_info, nano::account, &rep_info::account>>>>
	reps;
	nano::uint128_t trended_m;
	nano::uint128_t online_m;
	nano::uint128_t minimum;

	friend class election_quorum_minimum_update_weight_before_quorum_checks_Test;
	friend std::unique_ptr<container_info_component> collect_container_info (online_reps & online_reps, std::string const & name);
};

std::unique_ptr<container_info_component> collect_container_info (online_reps & online_reps, std::string const & name);
}
