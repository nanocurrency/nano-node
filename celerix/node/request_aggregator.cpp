#include <celerix/lib/blocks.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/endpoint.hpp>
#include <celerix/node/local_vote_history.hpp>
#include <celerix/node/network.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/nodeconfig.hpp>
#include <celerix/node/request_aggregator.hpp>
#include <celerix/node/vote_generator.hpp>
#include <celerix/node/vote_router.hpp>
#include <celerix/node/wallet.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/secure/ledger_set_confirmed.hpp>
#include <celerix/store/component.hpp>

celerix::request_aggregator::request_aggregator (request_aggregator_config const & config_a, celerix::node & node_a, celerix::stats & stats_a, celerix::vote_generator & generator_a, celerix::vote_generator & final_generator_a, celerix::local_vote_history & history_a, celerix::ledger & ledger_a, celerix::wallets & wallets_a, celerix::vote_router & vote_router_a) :
	config{ config_a },
	network_constants{ node_a.network_params.network },
	stats (stats_a),
	local_votes (history_a),
	ledger (ledger_a),
	wallets (wallets_a),
	vote_router{ vote_router_a },
	generator (generator_a),
	final_generator (final_generator_a)
{
	queue.max_size_query = [this] (auto const & origin) {
		return config.max_queue;
	};
	queue.priority_query = [this] (auto const & origin) {
		return 1;
	};
}

celerix::request_aggregator::~request_aggregator ()
{
	debug_assert (threads.empty ());
}

void celerix::request_aggregator::start ()
{
	debug_assert (threads.empty ());

	for (auto i = 0; i < config.threads; ++i)
	{
		threads.emplace_back ([this] () {
			celerix::thread_role::set (celerix::thread_role::name::request_aggregator);
			run ();
		});
	}
}

void celerix::request_aggregator::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	for (auto & thread : threads)
	{
		if (thread.joinable ())
		{
			thread.join ();
		}
	}
	threads.clear ();
}

std::size_t celerix::request_aggregator::size () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return queue.size ();
}

bool celerix::request_aggregator::empty () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return queue.empty ();
}

bool celerix::request_aggregator::request (request_type const & request, std::shared_ptr<celerix::transport::channel> const & channel)
{
	release_assert (channel != nullptr);

	// This should be checked before calling request
	debug_assert (wallets.reps ().voting > 0);
	debug_assert (!request.empty ());

	bool added = false;
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		added = queue.push ({ request, channel }, { celerix::no_value{}, channel });
	}
	if (added)
	{
		stats.inc (celerix::stat::type::request_aggregator, celerix::stat::detail::request);
		stats.add (celerix::stat::type::request_aggregator, celerix::stat::detail::request_hashes, request.size ());

		condition.notify_one ();
	}
	else
	{
		stats.inc (celerix::stat::type::request_aggregator, celerix::stat::detail::overfill);
		stats.add (celerix::stat::type::request_aggregator, celerix::stat::detail::overfill_hashes, request.size ());
	}

	// TODO: This stat is for compatibility with existing tests and is in principle unnecessary
	stats.inc (celerix::stat::type::aggregator, added ? celerix::stat::detail::aggregator_accepted : celerix::stat::detail::aggregator_dropped);

	return added;
}

void celerix::request_aggregator::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (celerix::stat::type::request_aggregator, celerix::stat::detail::loop);

		if (!queue.empty ())
		{
			run_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
		else
		{
			condition.wait (lock, [&] { return stopped || !queue.empty (); });
		}
	}
}

void celerix::request_aggregator::run_batch (celerix::unique_lock<celerix::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	debug_assert (config.batch_size > 0);
	auto batch = queue.next_batch (config.batch_size);

	lock.unlock ();

	auto transaction = ledger.tx_begin_read ();

	for (auto const & [value, origin] : batch)
	{
		auto const & [request, channel] = value;

		transaction.refresh_if_needed ();

		if (!channel->max (celerix::transport::traffic_type::vote_reply))
		{
			process (transaction, request, channel);
		}
		else
		{
			stats.inc (celerix::stat::type::request_aggregator, celerix::stat::detail::channel_full, stat::dir::out);
		}
	}
}

void celerix::request_aggregator::process (celerix::secure::transaction const & transaction, request_type const & request, std::shared_ptr<celerix::transport::channel> const & channel)
{
	auto const remaining = aggregate (transaction, request, channel);

	if (!remaining.remaining_normal.empty ())
	{
		stats.inc (celerix::stat::type::request_aggregator_replies, celerix::stat::detail::normal_vote);

		// Generate votes for the remaining hashes
		auto const generated = generator.generate (remaining.remaining_normal, channel);
		stats.add (celerix::stat::type::requests, celerix::stat::detail::requests_cannot_vote, stat::dir::in, remaining.remaining_normal.size () - generated);
	}
	if (!remaining.remaining_final.empty ())
	{
		stats.inc (celerix::stat::type::request_aggregator_replies, celerix::stat::detail::final_vote);

		// Generate final votes for the remaining hashes
		auto const generated = final_generator.generate (remaining.remaining_final, channel);
		stats.add (celerix::stat::type::requests, celerix::stat::detail::requests_cannot_vote, stat::dir::in, remaining.remaining_final.size () - generated);
	}
}

void celerix::request_aggregator::erase_duplicates (std::vector<std::pair<celerix::block_hash, celerix::root>> & requests_a) const
{
	std::sort (requests_a.begin (), requests_a.end (), [] (auto const & pair1, auto const & pair2) {
		return pair1.first < pair2.first;
	});
	requests_a.erase (std::unique (requests_a.begin (), requests_a.end (), [] (auto const & pair1, auto const & pair2) {
		return pair1.first == pair2.first;
	}),
	requests_a.end ());
}

// This filters candidates for vote generation, the final decision and necessary checks are also performed by the vote generator
auto celerix::request_aggregator::aggregate (celerix::secure::transaction const & transaction, request_type const & requests_a, std::shared_ptr<celerix::transport::channel> const & channel_a) const -> aggregate_result
{
	std::vector<std::shared_ptr<celerix::block>> to_generate;
	std::vector<std::shared_ptr<celerix::block>> to_generate_final;
	for (auto const & [hash, root] : requests_a)
	{
		// Ledger by hash
		std::shared_ptr<celerix::block> block = ledger.any.block_get (transaction, hash);

		// Ledger by root
		if (!block && !root.is_zero ())
		{
			// Search for block root
			if (auto successor = ledger.any.block_successor (transaction, root.as_block_hash ()))
			{
				block = ledger.any.block_get (transaction, successor.value ());
				release_assert (block);
			}
		}

		auto should_generate_final_vote = [&] (auto const & block) {
			release_assert (block);

			// Check if final vote is set for this block
			if (auto final_hash = ledger.store.final_vote.get (transaction, block->qualified_root ()))
			{
				return final_hash == block->hash ();
			}
			// If the final vote is not set, generate vote if the block is confirmed
			else
			{
				return ledger.confirmed.block_exists (transaction, block->hash ());
			}
		};

		if (block)
		{
			if (should_generate_final_vote (block))
			{
				to_generate_final.push_back (block);
				stats.inc (celerix::stat::type::requests, celerix::stat::detail::requests_final);
			}
			else
			{
				stats.inc (celerix::stat::type::requests, celerix::stat::detail::requests_non_final);
			}
		}
		else
		{
			stats.inc (celerix::stat::type::requests, celerix::stat::detail::requests_unknown);
		}
	}

	return {
		.remaining_normal = to_generate,
		.remaining_final = to_generate_final
	};
}

celerix::container_info celerix::request_aggregator::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.add ("queue", queue.container_info ());
	return info;
}

/*
 * request_aggregator_config
 */

celerix::error celerix::request_aggregator_config::serialize (celerix::tomlconfig & toml) const
{
	toml.put ("max_queue", max_queue, "Maximum number of queued requests per peer. \ntype:uint64");
	toml.put ("threads", threads, "Number of threads for request processing. \ntype:uint64");
	toml.put ("batch_size", batch_size, "Number of requests to process in a single batch. \ntype:uint64");

	return toml.get_error ();
}

celerix::error celerix::request_aggregator_config::deserialize (celerix::tomlconfig & toml)
{
	toml.get ("max_queue", max_queue);
	toml.get ("threads", threads);
	toml.get ("batch_size", batch_size);

	return toml.get_error ();
}
