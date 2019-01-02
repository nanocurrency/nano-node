#include <nano/node/voting.hpp>

#include <nano/node/node.hpp>

nano::vote_generator::vote_generator (nano::node & node_a, std::chrono::milliseconds wait_a) :
node (node_a),
wait (wait_a),
stopped (false),
started (false),
thread ([this]() { run (); })
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
}

void nano::vote_generator::add (nano::block_hash const & hash_a)
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		hashes.push_back (hash_a);
	}
	condition.notify_all ();
}

void nano::vote_generator::stop ()
{
	std::unique_lock<std::mutex> lock (mutex);
	stopped = true;

	lock.unlock ();
	condition.notify_all ();

	if (thread.joinable ())
	{
		thread.join ();
	}
}

void nano::vote_generator::cache_add (std::shared_ptr<nano::vote> const & vote_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	for (auto & block : vote_a->blocks)
	{
		auto hash (boost::get<nano::block_hash> (block));
		auto existing (votes_cache.find (hash));
		if (existing == votes_cache.end ())
		{
			// Clean old votes
			if (cache_order.size () >= max_cache)
			{
				auto old_hash (cache_order.front ());
				cache_order.pop_front ();
				votes_cache.erase (old_hash);
			}
			// Insert new votes (new hash)
			votes_cache.insert (std::make_pair (hash, std::vector<std::shared_ptr<nano::vote>> (1, vote_a)));
			cache_order.push_back (hash);
		}
		else
		{
			// Insert new votes (old hash)
			existing->second.push_back (vote_a);
		}
	}
}

std::vector<std::shared_ptr<nano::vote>> nano::vote_generator::cache_find (nano::block_hash const & hash_a)
{
	std::vector<std::shared_ptr<nano::vote>> result;
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (votes_cache.find (hash_a));
	if (existing != votes_cache.end ())
	{
		result = existing->second;
	}
	return result;
}

void nano::vote_generator::send (std::unique_lock<std::mutex> & lock_a)
{
	std::vector<nano::block_hash> hashes_l;
	hashes_l.reserve (12);
	while (!hashes.empty () && hashes_l.size () < 12)
	{
		hashes_l.push_back (hashes.front ());
		hashes.pop_front ();
	}
	lock_a.unlock ();
	{
		auto transaction (node.store.tx_begin_read ());
		node.wallets.foreach_representative (transaction, [this, &hashes_l, &transaction](nano::public_key const & pub_a, nano::raw_key const & prv_a) {
			auto vote (this->node.store.vote_generate (transaction, pub_a, prv_a, hashes_l));
			this->node.vote_processor.vote (vote, this->node.network.endpoint ());
			this->cache_add (vote);
		});
	}
	lock_a.lock ();
}

void nano::vote_generator::run ()
{
	nano::thread_role::set (nano::thread_role::name::voting);
	std::unique_lock<std::mutex> lock (mutex);
	started = true;
	lock.unlock ();
	condition.notify_all ();
	lock.lock ();
	auto min (std::numeric_limits<std::chrono::steady_clock::time_point>::min ());
	auto cutoff (min);
	while (!stopped)
	{
		auto now (std::chrono::steady_clock::now ());
		if (hashes.size () >= 12)
		{
			send (lock);
		}
		else if (cutoff == min) // && hashes.size () < 12
		{
			cutoff = now + wait;
			condition.wait_until (lock, cutoff);
		}
		else if (now < cutoff) // && hashes.size () < 12
		{
			condition.wait_until (lock, cutoff);
		}
		else // now >= cutoff && hashes.size () < 12
		{
			cutoff = min;
			if (!hashes.empty ())
			{
				send (lock);
			}
			else
			{
				condition.wait (lock);
			}
		}
	}
}
