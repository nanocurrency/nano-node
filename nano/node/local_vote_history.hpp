#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <memory>
#include <vector>

namespace mi = boost::multi_index;

namespace nano
{
class voting_constants;
}

namespace nano
{
class local_vote_history final
{
	class local_vote final
	{
	public:
		local_vote (nano::root const & root_a, nano::block_hash const & hash_a, std::shared_ptr<nano::vote> const & vote_a) :
			root (root_a),
			hash (hash_a),
			vote (vote_a)
		{
		}
		nano::root root;
		nano::block_hash hash;
		std::shared_ptr<nano::vote> vote;
	};

public:
	local_vote_history (nano::voting_constants const & constants) :
		constants{ constants }
	{
	}
	void add (nano::root const & root_a, nano::block_hash const & hash_a, std::shared_ptr<nano::vote> const & vote_a);
	void erase (nano::root const & root_a);

	std::vector<std::shared_ptr<nano::vote>> votes (nano::root const & root_a, nano::block_hash const & hash_a, bool const is_final_a = false) const;
	bool exists (nano::root const &) const;
	std::size_t size () const;

	nano::container_info container_info () const;

private:
	// clang-format off
	boost::multi_index_container<local_vote,
	mi::indexed_by<
		mi::hashed_non_unique<mi::tag<class tag_root>,
			mi::member<local_vote, nano::root, &local_vote::root>>,
		mi::sequenced<mi::tag<class tag_sequence>>>>
	history;
	// clang-format on

	nano::voting_constants const & constants;
	void clean ();
	std::vector<std::shared_ptr<nano::vote>> votes (nano::root const & root_a) const;
	// Only used in Debug
	bool consistency_check (nano::root const &) const;
	mutable nano::mutex mutex;

	friend class local_vote_history_basic_Test;
};
}
