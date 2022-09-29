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

void nano::unchecked_map::for_each (std::function<void (nano::unchecked_key const &, nano::unchecked_info const &)> action, std::function<bool ()> predicate)
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	for (auto i = entries.begin (), n = entries.end (); predicate () && i != n; ++i)
	{
		action (i->key, i->info);
	}
}

void nano::unchecked_map::for_each (nano::hash_or_account const & dependency, std::function<void (nano::unchecked_key const &, nano::unchecked_info const &)> action, std::function<bool ()> predicate)
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	for (auto i = entries.get<tag_root> ().lower_bound (nano::unchecked_key{ dependency, 0 }), n = entries.get<tag_root> ().end (); predicate () && i != n && i->key.key () == dependency; ++i)
	{
		action (i->key, i->info);
	}
}

std::vector<nano::unchecked_info> nano::unchecked_map::get (nano::block_hash const & hash)
{
	std::vector<nano::unchecked_info> result;
	for_each (hash, [&result] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
		result.push_back (info);
	});
	return result;
}

bool nano::unchecked_map::exists (nano::unchecked_key const & key) const
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	return entries.get<tag_root> ().count (key) != 0;
}

void nano::unchecked_map::del (nano::unchecked_key const & key)
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	auto erased = entries.get<tag_root> ().erase (key);
	debug_assert (erased);
}

void nano::unchecked_map::clear ()
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	entries.clear ();
}

size_t nano::unchecked_map::count () const
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	return entries.size ();
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

nano::unchecked_map::item_visitor::item_visitor (unchecked_map & unchecked) :
	unchecked{ unchecked }
{
}

void nano::unchecked_map::item_visitor::operator() (insert const & item)
{
	auto const & [dependency, info] = item;
	unchecked.insert_impl (dependency, info);
}

void nano::unchecked_map::item_visitor::operator() (query const & item)
{
	unchecked.query_impl (item.hash);
}

void nano::unchecked_map::write_buffer (decltype (buffer) const & back_buffer)
{
	auto transaction = store.tx_begin_write ();
	item_visitor visitor{ *this };
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

void nano::unchecked_map::insert_impl (nano::hash_or_account const & dependency, nano::unchecked_info const & info)
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	// Check if we should be using memory but the memory container hasn't been constructed i.e. we're transitioning from disk to memory.
	nano::unchecked_key key{ dependency, info.block->hash () };
	entries.get<tag_root> ().insert ({ key, info });
	while (entries.size () > mem_block_count_max)
	{
		entries.get<tag_sequenced> ().pop_front ();
	}
}

void nano::unchecked_map::query_impl (nano::block_hash const & hash)
{
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };

	std::deque<nano::unchecked_key> delete_queue;
	for_each (hash, [this, &delete_queue] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
		delete_queue.push_back (key);
		satisfied (info);
	});
	if (!disable_delete)
	{
		for (auto const & key : delete_queue)
		{
			del (key);
		}
	}
}