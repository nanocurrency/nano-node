#include <rai/node/voting.hpp>

#include <rai/node/node.hpp>

rai::vote_generator::vote_generator (rai::node & node_a, std::chrono::milliseconds wait_a) :
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

void rai::vote_generator::add (rai::block_hash const & hash_a)
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		hashes.push_back (hash_a);
	}
	condition.notify_all ();
}

void rai::vote_generator::stop ()
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

void rai::vote_generator::send (std::unique_lock<std::mutex> & lock_a)
{
	std::vector<rai::block_hash> hashes_l;
	hashes_l.reserve (12);
	while (!hashes.empty () && hashes_l.size () < 12)
	{
		hashes_l.push_back (hashes.front ());
		hashes.pop_front ();
	}
	lock_a.unlock ();
	{
		auto transaction (node.store.tx_begin_read ());
		node.wallets.foreach_representative (transaction, [this, &hashes_l, &transaction](rai::public_key const & pub_a, rai::raw_key const & prv_a) {
			auto vote (this->node.store.vote_generate (transaction, pub_a, prv_a, hashes_l));
			this->node.vote_processor.vote (vote, this->node.network.endpoint ());
		});
	}
	lock_a.lock ();
}

void rai::vote_generator::run ()
{
	rai::thread_role::set (rai::thread_role::name::voting);
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
