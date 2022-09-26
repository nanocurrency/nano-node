#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/store.hpp>

#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <thread>
#include <unordered_map>

namespace mi = boost::multi_index;

namespace nano
{
class store;
class transaction;
class unchecked_info;
class unchecked_key;
class write_transaction;
class unchecked_map
{
public:
	unchecked_map (nano::store & store, bool const & do_delete);
	~unchecked_map ();

	void put (nano::hash_or_account const & dependency, nano::unchecked_info const & info);
	void for_each (
	nano::transaction const & transaction, std::function<void (nano::unchecked_key const &, nano::unchecked_info const &)> action, std::function<bool ()> predicate = [] () { return true; });
	void for_each (
	nano::transaction const & transaction, nano::hash_or_account const & dependency, std::function<void (nano::unchecked_key const &, nano::unchecked_info const &)> action, std::function<bool ()> predicate = [] () { return true; });
	std::vector<nano::unchecked_info> get (nano::transaction const &, nano::block_hash const &);
	bool exists (nano::transaction const & transaction, nano::unchecked_key const & key) const;
	void del (nano::write_transaction const & transaction, nano::unchecked_key const & key);
	void clear (nano::write_transaction const & transaction);
	size_t count (nano::transaction const & transaction) const;
	void stop ();
	void flush ();

	std::function<bool ()> use_memory = [] () { return true; };

public: // Trigger requested dependencies
	void trigger (nano::hash_or_account const & dependency);
	std::function<void (nano::unchecked_info const &)> satisfied{ [] (nano::unchecked_info const &) {} };

private:
	using insert = std::pair<nano::hash_or_account, nano::unchecked_info>;
	using query = nano::hash_or_account;
	class item_visitor : boost::static_visitor<>
	{
	public:
		item_visitor (unchecked_map & unchecked, nano::write_transaction const & transaction);
		void operator() (insert const & item);
		void operator() (query const & item);
		unchecked_map & unchecked;
		nano::write_transaction const & transaction;
	};
	void run ();
	void insert_impl (nano::write_transaction const & transaction, nano::hash_or_account const & dependency, nano::unchecked_info const & info);
	void query_impl (nano::write_transaction const & transaction, nano::block_hash const & hash);
	nano::store & store;
	bool const & disable_delete;
	std::deque<boost::variant<insert, query>> buffer;
	std::deque<boost::variant<insert, query>> back_buffer;
	bool writing_back_buffer{ false };
	bool stopped{ false };
	nano::condition_variable condition;
	nano::mutex mutex;
	std::thread thread;
	void write_buffer (decltype (buffer) const & back_buffer);

	static size_t constexpr mem_block_count_max = 256'000;

	friend class item_visitor;

private: // In memory store
	class entry
	{
	public:
		nano::unchecked_key key;
		nano::unchecked_info info;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_root {};

	using ordered_unchecked = boost::multi_index_container<entry,
		mi::indexed_by<
			mi::random_access<mi::tag<tag_sequenced>>,
			mi::ordered_unique<mi::tag<tag_root>,
				mi::member<entry, nano::unchecked_key, &entry::key>>>>;
	// clang-format on
	std::unique_ptr<ordered_unchecked> entries;

	mutable std::recursive_mutex entries_mutex;
};
}
