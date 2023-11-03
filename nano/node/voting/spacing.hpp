#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>

namespace mi = boost::multi_index;

namespace nano::voting
{
class spacing final
{
	class entry
	{
	public:
		nano::root root;
		std::chrono::steady_clock::time_point time;
		nano::block_hash hash;
	};

	boost::multi_index_container<entry,
	mi::indexed_by<
	mi::hashed_non_unique<mi::tag<class tag_root>,
	mi::member<entry, nano::root, &entry::root>>,
	mi::ordered_non_unique<mi::tag<class tag_time>,
	mi::member<entry, std::chrono::steady_clock::time_point, &entry::time>>>>
	recent;
	std::chrono::milliseconds const delay;
	void trim ();

public:
	spacing (std::chrono::milliseconds const & delay) :
		delay{ delay }
	{
	}
	bool votable (nano::root const & root_a, nano::block_hash const & hash_a) const;
	void flag (nano::root const & root_a, nano::block_hash const & hash_a);
	std::size_t size () const;
};
} // namespace nano
