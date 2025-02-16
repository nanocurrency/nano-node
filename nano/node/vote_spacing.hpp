#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>

namespace mi = boost::multi_index;

namespace nano
{
class vote_spacing final
{
public:
	explicit vote_spacing (std::chrono::milliseconds const & delay) :
		delay{ delay }
	{
	}

	bool votable (nano::root const & root, nano::block_hash const & hash) const;
	void flag (nano::root const & root, nano::block_hash const & hash);
	std::size_t size () const;

private:
	void trim ();

private:
	std::chrono::milliseconds const delay;

	struct entry
	{
		nano::root root;
		std::chrono::steady_clock::time_point time;
		nano::block_hash hash;
	};

	// clang-format off
	class tag_root {};
	class tag_time {};

	using ordered_votes = boost::multi_index_container<entry,
	mi::indexed_by<
	    mi::hashed_non_unique<mi::tag<tag_root>,
			mi::member<entry, nano::root, &entry::root>>,
	    mi::ordered_non_unique<mi::tag<tag_time>,
			mi::member<entry, std::chrono::steady_clock::time_point, &entry::time>>
	>>;
	// clang-format on

	ordered_votes recent;
};
}