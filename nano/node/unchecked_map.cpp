#include <nano/lib/locks.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/unchecked_map.hpp>
#include <nano/secure/store.hpp>

#include <boost/range/join.hpp>

nano::unchecked_map::unchecked_map (nano::store & store, bool const & disable_delete) :
	store{ store },
	disable_delete{ disable_delete },
	thread{ [this] () { run (); } }
{
}

nano::unchecked_map::~unchecked_map ()
{
	stop ();
	thread.join ();
}

void nano::unchecked_map::put (nano::hash_or_account const & dependency, nano::unchecked_info const & info)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	buffer.push_back (std::make_pair (dependency, info));
	lock.unlock ();
	condition.notify_all (); // Notify run ()
}

void nano::unchecked_map::for_each (
nano::transaction const & transaction, std::function<void (nano::unchecked_key const &, nano::unchecked_info const &)> action, std::function<bool ()> predicate)
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	if (entries == nullptr)
	{
		for (auto [i, n] = store.unchecked.full_range (transaction); predicate () && i != n; ++i)
		{
			action (i->first, i->second);
		}
	}
	else
	{
		for (auto i = entries->begin (), n = entries->end (); predicate () && i != n; ++i)
		{
			action (i->key, i->info);
		}
	}
}

void nano::unchecked_map::for_each (
nano::transaction const & transaction, nano::hash_or_account const & dependency, std::function<void (nano::unchecked_key const &, nano::unchecked_info const &)> action, std::function<bool ()> predicate)
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	if (entries == nullptr)
	{
		for (auto [i, n] = store.unchecked.equal_range (transaction, dependency.as_block_hash ()); predicate () && i->first.key () == dependency && i != n; ++i)
		{
			action (i->first, i->second);
		}
	}
	else
	{
		for (auto i = entries->template get<tag_root> ().lower_bound (nano::unchecked_key{ dependency, 0 }), n = entries->template get<tag_root> ().end (); predicate () && i != n && i->key.key () == dependency; ++i)
		{
			action (i->key, i->info);
		}
	}
}

std::vector<nano::unchecked_info> nano::unchecked_map::get (nano::transaction const & transaction, nano::block_hash const & hash)
{
	std::vector<nano::unchecked_info> result;
	for_each (transaction, hash, [&result] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
		result.push_back (info);
	});
	return result;
}

bool nano::unchecked_map::exists (nano::transaction const & transaction, nano::unchecked_key const & key) const
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	if (entries == nullptr)
	{
		return store.unchecked.exists (transaction, key);
	}
	else
	{
		return entries->template get<tag_root> ().count (key) != 0;
	}
}

void nano::unchecked_map::del (nano::write_transaction const & transaction, nano::unchecked_key const & key)
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	if (entries == nullptr)
	{
		store.unchecked.del (transaction, key);
	}
	else
	{
		auto erased = entries->template get<tag_root> ().erase (key);
		release_assert (erased);
	}
}

void nano::unchecked_map::clear (nano::write_transaction const & transaction)
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	if (entries == nullptr)
	{
		store.unchecked.clear (transaction);
	}
	else
	{
		entries->clear ();
	}
}

size_t nano::unchecked_map::count (nano::transaction const & transaction) const
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	if (entries == nullptr)
	{
		return store.unchecked.count (transaction);
	}
	else
	{
		return entries->size ();
	}
}

void nano::unchecked_map::stop ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	if (!stopped)
	{
		stopped = true;
		condition.notify_all (); // Notify flush (), run ()
	}
}

void nano::unchecked_map::flush ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	condition.wait (lock, [this] () {
		return stopped || (buffer.empty () && back_buffer.empty () && !writing_back_buffer);
	});
}

void nano::unchecked_map::trigger (nano::hash_or_account const & dependency)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	buffer.push_back (dependency);
	debug_assert (buffer.back ().which () == 1); // which stands for "query".
	lock.unlock ();
	condition.notify_all (); // Notify run ()
}

nano::unchecked_map::item_visitor::item_visitor (unchecked_map & unchecked, nano::write_transaction const & transaction) :
	unchecked{ unchecked },
	transaction{ transaction }
{
}

void nano::unchecked_map::item_visitor::operator() (insert const & item)
{
	auto const & [dependency, info] = item;
	unchecked.insert_impl (transaction, dependency, info);
}

void nano::unchecked_map::item_visitor::operator() (query const & item)
{
	unchecked.query_impl (transaction, item.hash);
}

void nano::unchecked_map::write_buffer (decltype (buffer) const & back_buffer)
{
	auto transaction = store.tx_begin_write ();
	item_visitor visitor{ *this, transaction };
	for (auto const & item : back_buffer)
	{
		boost::apply_visitor (visitor, item);
	}
}

void nano::unchecked_map::run ()
{
	nano::thread_role::set (nano::thread_role::name::unchecked);
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		if (!buffer.empty ())
		{
			back_buffer.swap (buffer);
			writing_back_buffer = true;
			lock.unlock ();
			write_buffer (back_buffer);
			lock.lock ();
			writing_back_buffer = false;
			back_buffer.clear ();
		}
		else
		{
			condition.notify_all (); // Notify flush ()
			condition.wait (lock, [this] () {
				return stopped || !buffer.empty ();
			});
		}
	}
}

void nano::unchecked_map::insert_impl (nano::write_transaction const & transaction, nano::hash_or_account const & dependency, nano::unchecked_info const & info)
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	// Check if we should be using memory but the memory container hasn't been constructed i.e. we're transitioning from disk to memory.
	if (entries == nullptr && use_memory ())
	{
		auto entries_new = std::make_unique<typename decltype (entries)::element_type> ();
		for_each (
		transaction, [&entries_new] (nano::unchecked_key const & key, nano::unchecked_info const & info) { entries_new->template get<tag_root> ().insert ({ key, info }); }, [&] () { return entries_new->size () < mem_block_count_max; });
		clear (transaction);
		entries = std::move (entries_new);
	}
	if (entries == nullptr)
	{
		store.unchecked.put (transaction, dependency, { info.block, info.account });
	}
	else
	{
		nano::unchecked_key key{ dependency, info.block->hash () };
		entries->template get<tag_root> ().insert ({ key, info });
		while (entries->size () > mem_block_count_max)
		{
			entries->template get<tag_sequenced> ().pop_front ();
		}
	}
}

void nano::unchecked_map::query_impl (nano::write_transaction const & transaction, nano::block_hash const & hash)
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };

	std::deque<nano::unchecked_key> delete_queue;
	for_each (transaction, hash, [this, &delete_queue] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
		delete_queue.push_back (key);
		satisfied (info);
	});
	if (!disable_delete)
	{
		for (auto const & key : delete_queue)
		{
			del (transaction, key);
		}
	}
}
