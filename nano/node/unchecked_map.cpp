#include <nano/lib/locks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/unchecked_map.hpp>

nano::unchecked_map::unchecked_map (unsigned & max_unchecked_blocks, nano::stats & stats, bool const & disable_delete) :
	max_unchecked_blocks{ max_unchecked_blocks },
	stats{ stats },
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
	nano::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	nano::unchecked_key key{ dependency, info.block->hash () };
	entries.get<tag_root> ().insert ({ key, info });

	if (entries.size () > max_unchecked_blocks)
	{
		entries.get<tag_sequenced> ().pop_front ();
	}
	stats.inc (nano::stat::type::unchecked, nano::stat::detail::put);
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
	for (auto i = entries.template get<tag_root> ().lower_bound (nano::unchecked_key{ dependency, 0 }), n = entries.template get<tag_root> ().end (); predicate () && i != n && i->key.key () == dependency.as_block_hash (); ++i)
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

std::size_t nano::unchecked_map::count () const
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
	buffer.emplace_back (dependency);
	lock.unlock ();
	stats.inc (nano::stat::type::unchecked, nano::stat::detail::trigger);
	condition.notify_all (); // Notify run ()
}

void nano::unchecked_map::process_queries (decltype (buffer) const & back_buffer)
{
	for (auto const & item : back_buffer)
	{
		query_impl (item.hash);
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
			process_queries (back_buffer);
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

void nano::unchecked_map::query_impl (nano::block_hash const & hash)
{
	std::deque<nano::unchecked_key> delete_queue;
	for_each (hash, [this, &delete_queue] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
		delete_queue.push_back (key);
		stats.inc (nano::stat::type::unchecked, nano::stat::detail::satisfied);
		satisfied.notify (info);
	});
	if (!disable_delete)
	{
		for (auto const & key : delete_queue)
		{
			del (key);
		}
	}
}

std::unique_ptr<nano::container_info_component> nano::unchecked_map::collect_container_info (const std::string & name)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "entries", entries.size (), sizeof (decltype (entries)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "queries", buffer.size (), sizeof (decltype (buffer)::value_type) }));
	return composite;
}
