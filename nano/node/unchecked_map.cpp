#include <nano/lib/locks.hpp>
#include <nano/lib/threading.hpp>
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

auto nano::unchecked_map::equal_range (nano::transaction const & transaction, nano::block_hash const & dependency) -> std::pair<iterator, iterator>
{
	return store.unchecked.equal_range (transaction, dependency);
}

auto nano::unchecked_map::full_range (nano::transaction const & transaction) -> std::pair<iterator, iterator>
{
	return store.unchecked.full_range (transaction);
}

std::vector<nano::unchecked_info> nano::unchecked_map::get (nano::transaction const & transaction, nano::block_hash const & hash)
{
	return store.unchecked.get (transaction, hash);
}

bool nano::unchecked_map::exists (nano::transaction const & transaction, nano::unchecked_key const & key) const
{
	return store.unchecked.exists (transaction, key);
}

void nano::unchecked_map::del (nano::write_transaction const & transaction, nano::unchecked_key const & key)
{
	store.unchecked.del (transaction, key);
}

void nano::unchecked_map::clear (nano::write_transaction const & transaction)
{
	store.unchecked.clear (transaction);
}

size_t nano::unchecked_map::count (nano::transaction const & transaction) const
{
	return store.unchecked.count (transaction);
}

void nano::unchecked_map::stop ()
{
	if (!stopped.exchange (true))
	{
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
	unchecked.store.unchecked.put (transaction, dependency, info);
}

void nano::unchecked_map::item_visitor::operator() (query const & item)
{
	auto [i, n] = unchecked.store.unchecked.equal_range (transaction, item.hash);
	std::deque<nano::unchecked_key> delete_queue;
	for (; i != n; ++i)
	{
		auto const & key = i->first;
		auto const & info = i->second;
		delete_queue.push_back (key);
		unchecked.satisfied (info);
	}
	if (!unchecked.disable_delete)
	{
		for (auto const & key : delete_queue)
		{
			unchecked.del (transaction, key);
		}
	}
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
