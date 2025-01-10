#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/observer_set.hpp>
#include <celerix/secure/common.hpp>

#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <thread>

namespace mi = boost::multi_index;

namespace celerix
{
class stats;

class unchecked_map
{
public:
	unchecked_map (unsigned const max_unchecked_blocks, celerix::stats &, bool const & do_delete);
	~unchecked_map ();

	void start ();
	void stop ();

	void put (celerix::hash_or_account const & dependency, celerix::unchecked_info const & info);
	void for_each (
	std::function<void (celerix::unchecked_key const &, celerix::unchecked_info const &)> action, std::function<bool ()> predicate = [] () { return true; });
	void for_each (
	celerix::hash_or_account const & dependency, std::function<void (celerix::unchecked_key const &, celerix::unchecked_info const &)> action, std::function<bool ()> predicate = [] () { return true; });
	std::vector<celerix::unchecked_info> get (celerix::block_hash const &);
	bool exists (celerix::unchecked_key const & key) const;
	void del (celerix::unchecked_key const & key);
	void clear ();

	/**
	 * Trigger requested dependencies
	 */
	void trigger (celerix::hash_or_account const & dependency);

	size_t count () const; // Same as `entries_size ()`
	size_t entries_size () const;
	size_t queries_size () const;

	celerix::container_info container_info () const;

public: // Events
	celerix::observer_set<celerix::unchecked_info const &> satisfied;

private:
	void run ();
	void query_impl (celerix::block_hash const & hash);

private: // Dependencies
	celerix::stats & stats;

private:
	bool const & disable_delete;
	std::deque<celerix::hash_or_account> buffer;
	std::deque<celerix::hash_or_account> back_buffer;
	bool writing_back_buffer{ false };

	bool stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex; // Protects queries
	std::thread thread;

	unsigned const max_unchecked_blocks;

	void process_queries (decltype (buffer) const & back_buffer);

private:
	struct entry
	{
		celerix::unchecked_key key;
		celerix::unchecked_info info;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_root {};

	using ordered_unchecked = boost::multi_index_container<entry,
		mi::indexed_by<
			mi::sequenced<mi::tag<tag_sequenced>>,
			mi::ordered_unique<mi::tag<tag_root>,
				mi::member<entry, celerix::unchecked_key, &entry::key>>>>;
	// clang-format on
	ordered_unchecked entries;

	mutable std::recursive_mutex entries_mutex; // Protects entries
};
}
