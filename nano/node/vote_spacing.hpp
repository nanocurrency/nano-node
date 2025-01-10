#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/lib/numbers_templ.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>

namespace mi = boost::multi_index;

namespace celerix
{
class vote_spacing final
{
	class entry
	{
	public:
		celerix::root root;
		std::chrono::steady_clock::time_point time;
		celerix::block_hash hash;
	};

	boost::multi_index_container<entry,
	mi::indexed_by<
	mi::hashed_non_unique<mi::tag<class tag_root>,
	mi::member<entry, celerix::root, &entry::root>>,
	mi::ordered_non_unique<mi::tag<class tag_time>,
	mi::member<entry, std::chrono::steady_clock::time_point, &entry::time>>>>
	recent;
	std::chrono::milliseconds const delay;
	void trim ();

public:
	vote_spacing (std::chrono::milliseconds const & delay) :
		delay{ delay }
	{
	}
	bool votable (celerix::root const & root_a, celerix::block_hash const & hash_a) const;
	void flag (celerix::root const & root_a, celerix::block_hash const & hash_a);
	std::size_t size () const;
};
}
