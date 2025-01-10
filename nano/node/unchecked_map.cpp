#include <celerix/lib/blocks.hpp>
#include <celerix/lib/locks.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/stats_enums.hpp>
#include <celerix/lib/thread_roles.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/node/unchecked_map.hpp>

celerix::unchecked_map::unchecked_map (unsigned const max_unchecked_blocks, celerix::stats & stats, bool const & disable_delete) :
	max_unchecked_blocks{ max_unchecked_blocks },
	stats{ stats },
	disable_delete{ disable_delete }
{
}

celerix::unchecked_map::~unchecked_map ()
{
	debug_assert (!thread.joinable ());
}

void celerix::unchecked_map::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		celerix::thread_role::set (celerix::thread_role::name::unchecked);
		run ();
	});
}

void celerix::unchecked_map::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	if (thread.joinable ())
	{
		thread.join ();
	}
}

void celerix::unchecked_map::put (celerix::hash_or_account const & dependency, celerix::unchecked_info const & info)
{
	celerix::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	celerix::unchecked_key key{ dependency, info.block->hash () };
	entries.get<tag_root> ().insert ({ key, info });

	if (entries.size () > max_unchecked_blocks)
	{
		entries.get<tag_sequenced> ().pop_front ();
	}

	stats.inc (celerix::stat::type::unchecked, celerix::stat::detail::put);
}

void celerix::unchecked_map::for_each (std::function<void (celerix::unchecked_key const &, celerix::unchecked_info const &)> action, std::function<bool ()> predicate)
{
	celerix::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	for (auto i = entries.begin (), n = entries.end (); predicate () && i != n; ++i)
	{
		action (i->key, i->info);
	}
}

void celerix::unchecked_map::for_each (celerix::hash_or_account const & dependency, std::function<void (celerix::unchecked_key const &, celerix::unchecked_info const &)> action, std::function<bool ()> predicate)
{
	celerix::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	for (auto i = entries.template get<tag_root> ().lower_bound (celerix::unchecked_key{ dependency, 0 }), n = entries.template get<tag_root> ().end (); predicate () && i != n && i->key.key () == dependency.as_block_hash (); ++i)
	{
		action (i->key, i->info);
	}
}

std::vector<celerix::unchecked_info> celerix::unchecked_map::get (celerix::block_hash const & hash)
{
	std::vector<celerix::unchecked_info> result;
	for_each (hash, [&result] (celerix::unchecked_key const & key, celerix::unchecked_info const & info) {
		result.push_back (info);
	});
	return result;
}

bool celerix::unchecked_map::exists (celerix::unchecked_key const & key) const
{
	celerix::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	return entries.get<tag_root> ().count (key) != 0;
}

void celerix::unchecked_map::del (celerix::unchecked_key const & key)
{
	celerix::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	auto erased = entries.get<tag_root> ().erase (key);
	debug_assert (erased);
}

void celerix::unchecked_map::clear ()
{
	celerix::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	entries.clear ();
}

size_t celerix::unchecked_map::entries_size () const
{
	celerix::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	return entries.size ();
}

size_t celerix::unchecked_map::queries_size () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return buffer.size ();
}

size_t celerix::unchecked_map::count () const
{
	return entries_size ();
}

void celerix::unchecked_map::trigger (celerix::hash_or_account const & dependency)
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	buffer.emplace_back (dependency);
	lock.unlock ();
	stats.inc (celerix::stat::type::unchecked, celerix::stat::detail::trigger);
	condition.notify_all (); // Notify run ()
}

void celerix::unchecked_map::process_queries (decltype (buffer) const & back_buffer)
{
	for (auto const & item : back_buffer)
	{
		query_impl (item.hash);
	}
}

void celerix::unchecked_map::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
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
			condition.wait (lock, [this] () {
				return stopped || !buffer.empty ();
			});
		}
	}
}

void celerix::unchecked_map::query_impl (celerix::block_hash const & hash)
{
	std::deque<celerix::unchecked_key> delete_queue;
	for_each (hash, [this, &delete_queue] (celerix::unchecked_key const & key, celerix::unchecked_info const & info) {
		delete_queue.push_back (key);
		stats.inc (celerix::stat::type::unchecked, celerix::stat::detail::satisfied);
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

celerix::container_info celerix::unchecked_map::container_info () const
{
	celerix::container_info info;
	info.put ("entries", entries_size ());
	info.put ("queries", queries_size ());
	return info;
}
