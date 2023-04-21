#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <thread>

namespace mi = boost::multi_index;

namespace nano
{
class stats;

class unchecked_map
{
public:
	unchecked_map (nano::stats &, bool const & do_delete);
	~unchecked_map ();

	void put (nano::hash_or_account const & dependency, nano::unchecked_info const & info);
	void for_each (
	std::function<void (nano::unchecked_key const &, nano::unchecked_info const &)> action, std::function<bool ()> predicate = [] () { return true; });
	void for_each (
	nano::hash_or_account const & dependency, std::function<void (nano::unchecked_key const &, nano::unchecked_info const &)> action, std::function<bool ()> predicate = [] () { return true; });
	std::vector<nano::unchecked_info> get (nano::block_hash const &);
	bool exists (nano::unchecked_key const & key) const;
	void del (nano::unchecked_key const & key);
	void clear ();
	std::size_t count () const;
	void stop ();
	void flush ();

	/**
	 * Trigger requested dependencies
	 */
	void trigger (nano::hash_or_account const & dependency);

public: // Events
	nano::observer_set<nano::unchecked_info const &> satisfied;

private:
	void run ();
	void query_impl (nano::block_hash const & hash);

private: // Dependencies
	nano::stats & stats;

private:
	bool const & disable_delete;
	std::deque<nano::hash_or_account> buffer;
	std::deque<nano::hash_or_account> back_buffer;
	bool writing_back_buffer{ false };
	bool stopped{ false };
	nano::condition_variable condition;
	nano::mutex mutex;
	std::thread thread;

	void process_queries (decltype (buffer) const & back_buffer);

	static std::size_t constexpr mem_block_count_max = 64 * 1024;

private:
	struct entry
	{
		nano::unchecked_key key;
		nano::unchecked_info info;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_root {};

	using ordered_unchecked = boost::multi_index_container<entry,
		mi::indexed_by<
			mi::sequenced<mi::tag<tag_sequenced>>,
			mi::ordered_unique<mi::tag<tag_root>,
				mi::member<entry, nano::unchecked_key, &entry::key>>>>;
	// clang-format on
	ordered_unchecked entries;

	mutable std::recursive_mutex entries_mutex;

public: // Container info
	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const & name);
};
}
