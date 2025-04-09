#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <deque>
#include <memory>
#include <mutex>

namespace mi = boost::multi_index;

namespace nano
{
class fork_cache_config final
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	size_t max_size{ 1024 * 16 };
	size_t max_forks_per_root{ 10 };
};

class fork_cache final
{
public:
	fork_cache (fork_cache_config const &, nano::stats &);

	void put (std::shared_ptr<nano::block> fork);
	std::deque<std::shared_ptr<nano::block>> get (nano::qualified_root const & root) const;

	size_t size () const;
	bool contains (nano::qualified_root const & root) const;

	nano::container_info container_info () const;

private:
	fork_cache_config const & config;
	nano::stats & stats;

	struct entry
	{
		nano::qualified_root root;
		mutable std::deque<std::shared_ptr<nano::block>> forks;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_root {};

	using ordered_forks = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<entry, nano::qualified_root, &entry::root>>
	>>;
	// clang-format on

	ordered_forks roots;

	mutable std::mutex mutex;
};
}