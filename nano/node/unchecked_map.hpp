#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/store.hpp>

#include <atomic>
#include <thread>
#include <unordered_map>

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

public: // Trigger requested dependencies
	void trigger (nano::hash_or_account const & dependency);
	std::function<void (nano::unchecked_info const &)> satisfied{ [] (nano::unchecked_info const &) {} };

private:
	using iterator = nano::unchecked_store::iterator;
	std::pair<iterator, iterator> equal_range (nano::transaction const & transaction, nano::block_hash const & dependency);
	std::pair<iterator, iterator> full_range (nano::transaction const & transaction);

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
};
}
