#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/numbers_templ.hpp>
#include <celerix/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <memory>
#include <vector>

namespace mi = boost::multi_index;

namespace celerix
{
class voting_constants;
}

namespace celerix
{
class local_vote_history final
{
	class local_vote final
	{
	public:
		local_vote (celerix::root const & root_a, celerix::block_hash const & hash_a, std::shared_ptr<celerix::vote> const & vote_a) :
			root (root_a),
			hash (hash_a),
			vote (vote_a)
		{
		}
		celerix::root root;
		celerix::block_hash hash;
		std::shared_ptr<celerix::vote> vote;
	};

public:
	local_vote_history (celerix::voting_constants const & constants) :
		constants{ constants }
	{
	}
	void add (celerix::root const & root_a, celerix::block_hash const & hash_a, std::shared_ptr<celerix::vote> const & vote_a);
	void erase (celerix::root const & root_a);

	std::vector<std::shared_ptr<celerix::vote>> votes (celerix::root const & root_a, celerix::block_hash const & hash_a, bool const is_final_a = false) const;
	bool exists (celerix::root const &) const;
	std::size_t size () const;

	celerix::container_info container_info () const;

private:
	// clang-format off
	boost::multi_index_container<local_vote,
	mi::indexed_by<
		mi::hashed_non_unique<mi::tag<class tag_root>,
			mi::member<local_vote, celerix::root, &local_vote::root>>,
		mi::sequenced<mi::tag<class tag_sequence>>>>
	history;
	// clang-format on

	celerix::voting_constants const & constants;
	void clean ();
	std::vector<std::shared_ptr<celerix::vote>> votes (celerix::root const & root_a) const;
	// Only used in Debug
	bool consistency_check (celerix::root const &) const;
	mutable celerix::mutex mutex;

	friend class local_vote_history_basic_Test;
};
}
