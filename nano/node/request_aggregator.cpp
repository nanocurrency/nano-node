#include <nano/lib/stats.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/common.hpp>
#include <nano/node/network.hpp>
#include <nano/node/request_aggregator.hpp>
#include <nano/node/transport/udp.hpp>
#include <nano/node/voting.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/blockstore.hpp>

nano::request_aggregator::request_aggregator (nano::stat & stats_a, nano::network_constants const & network_constants_a, nano::votes_cache & cache_a, nano::block_store & store_a, nano::wallets & wallets_a) :
max_delay (network_constants_a.is_test_network () ? 50 : 300),
small_delay (network_constants_a.is_test_network () ? 10 : 50),
stats (stats_a),
votes_cache (cache_a),
store (store_a),
wallets (wallets_a),
// clang-format off
thread ([this]() { run (); })
{
	nano::unique_lock<std::mutex> lock (mutex);
	condition.wait (lock, [& started = started] { return started; });
}
// clang-format on

void nano::request_aggregator::add (std::shared_ptr<nano::transport::channel> & channel_a, std::vector<std::pair<nano::block_hash, nano::root>> const & hashes_roots_a)
{
	assert (wallets.rep_counts ().voting > 0);
	auto const endpoint (nano::transport::map_endpoint_to_v6 (channel_a->get_endpoint ()));
	nano::unique_lock<std::mutex> lock (mutex);
	auto & requests_by_endpoint (requests.get<tag_endpoint> ());
	auto existing (requests_by_endpoint.find (endpoint));
	if (existing == requests_by_endpoint.end ())
	{
		existing = requests_by_endpoint.emplace (channel_a).first;
	}
	// clang-format off
	requests_by_endpoint.modify (existing, [&hashes_roots_a, &channel_a, this](channel_pool & pool_a) {
		// This extends the lifetime of the channel, which is acceptable up to max_delay
		pool_a.channel = channel_a;
		auto new_deadline (std::min (pool_a.start + this->max_delay, std::chrono::steady_clock::now () + this->small_delay));
		pool_a.deadline = new_deadline;
		pool_a.hashes_roots.insert (pool_a.hashes_roots.begin (), hashes_roots_a.begin (), hashes_roots_a.end ());
	});
	// clang-format on
	if (requests.size () == 1)
	{
		lock.unlock ();
		condition.notify_all ();
	}
}

void nano::request_aggregator::run ()
{
	nano::thread_role::set (nano::thread_role::name::request_aggregator);
	nano::unique_lock<std::mutex> lock (mutex);
	started = true;
	lock.unlock ();
	condition.notify_all ();
	lock.lock ();
	while (!stopped)
	{
		if (!requests.empty ())
		{
			auto & requests_by_deadline (requests.get<tag_deadline> ());
			auto front (requests_by_deadline.begin ());
			if (front->deadline < std::chrono::steady_clock::now ())
			{
				auto pool (*front);
				auto transaction (store.tx_begin_read ());
				// Aggregate the current pool of requests, sending cached votes
				// Store a local copy of the remaining hashes and the channel
				auto remaining = aggregate (transaction, pool);
				auto channel = pool.channel;
				// Safely erase this pool
				requests_by_deadline.erase (front);
				if (!remaining.empty ())
				{
					lock.unlock ();
					// Generate votes for the remaining hashes
					generate (transaction, std::move (remaining), channel);
					lock.lock ();
				}
			}
			else
			{
				auto deadline = front->deadline;
				// clang-format off
				condition.wait_until (lock, deadline, [this, &deadline]() { return this->stopped || deadline < std::chrono::steady_clock::now (); });
				// clang-format on
			}
		}
		else
		{
			// clang-format off
			condition.wait_for (lock, small_delay, [this]() { return this->stopped || !this->requests.empty (); });
			// clang-format on
		}
	}
}

void nano::request_aggregator::stop ()
{
	{
		nano::lock_guard<std::mutex> guard (mutex);
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

std::size_t nano::request_aggregator::size ()
{
	nano::unique_lock<std::mutex> lock (mutex);
	return requests.size ();
}

bool nano::request_aggregator::empty ()
{
	return size () == 0;
}

std::vector<nano::block_hash> nano::request_aggregator::aggregate (nano::transaction const & transaction_a, channel_pool & pool_a) const
{
	std::vector<nano::block_hash> to_generate;
	std::vector<std::shared_ptr<nano::vote>> cached_votes;
	for (auto const & hash_root : pool_a.hashes_roots)
	{
		auto find_votes (votes_cache.find (hash_root.first));
		if (!find_votes.empty ())
		{
			cached_votes.insert (cached_votes.end (), find_votes.begin (), find_votes.end ());
		}
		else if (!hash_root.first.is_zero () && store.block_exists (transaction_a, hash_root.first))
		{
			to_generate.push_back (hash_root.first);
		}
		else if (!hash_root.second.is_zero ())
		{
			// Search for block root
			auto successor (store.block_successor (transaction_a, hash_root.second));
			// Search for account root
			if (successor.is_zero ())
			{
				nano::account_info info;
				auto error (store.account_get (transaction_a, hash_root.second, info));
				if (!error)
				{
					successor = info.open_block;
				}
			}
			if (!successor.is_zero ())
			{
				auto find_successor_votes (votes_cache.find (successor));
				if (!find_successor_votes.empty ())
				{
					cached_votes.insert (cached_votes.end (), find_successor_votes.begin (), find_successor_votes.end ());
				}
				else
				{
					to_generate.push_back (successor);
				}
				auto successor_block (store.block_get (transaction_a, successor));
				assert (successor_block != nullptr);
				nano::publish publish (successor_block);
				pool_a.channel->send (publish);
			}
			else
			{
				stats.inc (nano::stat::type::requests, nano::stat::detail::requests_ignored);
			}
		}
	}
	// Unique votes
	std::sort (cached_votes.begin (), cached_votes.end ());
	cached_votes.erase (std::unique (cached_votes.begin (), cached_votes.end ()), cached_votes.end ());
	for (auto const & vote : cached_votes)
	{
		nano::confirm_ack confirm (vote);
		pool_a.channel->send (confirm);
	}
	stats.add (nano::stat::type::requests, nano::stat::detail::requests_cached, stat::dir::in, cached_votes.size ());
	return to_generate;
}

void nano::request_aggregator::generate (nano::transaction const & transaction_a, std::vector<nano::block_hash> const hashes_a, std::shared_ptr<nano::transport::channel> & channel_a) const
{
	size_t generated_l = 0;
	auto i (hashes_a.begin ());
	auto n (hashes_a.end ());
	while (i != n)
	{
		std::vector<nano::block_hash> hashes_l;
		for (; i != n && hashes_l.size () < nano::network::confirm_ack_hashes_max; ++i)
		{
			hashes_l.push_back (*i);
		}
		// clang-format off
		wallets.foreach_representative ([this, &generated_l, &hashes_l, &channel_a, &transaction_a](nano::public_key const & pub_a, nano::raw_key const & prv_a) {
			auto vote (this->store.vote_generate (transaction_a, pub_a, prv_a, hashes_l));
			++generated_l;
			nano::confirm_ack confirm (vote);
			channel_a->send (confirm);
			this->votes_cache.add (vote);
		});
		// clang-format on
	}
	stats.add (nano::stat::type::requests, nano::stat::detail::requests_generated, stat::dir::in, generated_l);
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (nano::request_aggregator & aggregator, const std::string & name)
{
	auto pools_count = aggregator.size ();
	auto sizeof_element = sizeof (decltype (aggregator.requests)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "pools", pools_count, sizeof_element }));
	return composite;
}
