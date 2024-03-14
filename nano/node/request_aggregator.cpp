#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/common.hpp>
#include <nano/node/election.hpp>
#include <nano/node/local_vote_history.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/request_aggregator.hpp>
#include <nano/node/vote_generator.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/store/component.hpp>

nano::request_aggregator::request_aggregator (request_aggregator_config const & config_a, nano::node & node_a, nano::stats & stats_a, nano::vote_generator & generator_a, nano::vote_generator & final_generator_a, nano::local_vote_history & history_a, nano::ledger & ledger_a, nano::wallets & wallets_a, nano::vote_router & vote_router_a) :
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
	generator.set_reply_action ([this] (std::shared_ptr<nano::vote> const & vote_a, std::shared_ptr<nano::transport::channel> const & channel_a) {
		this->reply_action (vote_a, channel_a);
	});
	final_generator.set_reply_action ([this] (std::shared_ptr<nano::vote> const & vote_a, std::shared_ptr<nano::transport::channel> const & channel_a) {
		this->reply_action (vote_a, channel_a);
	});

	queue.max_size_query = [this] (auto const & origin) {
		return config.max_queue;
	};
	queue.priority_query = [this] (auto const & origin) {
		return 1;
	};
}

nano::request_aggregator::~request_aggregator ()
{
	debug_assert (threads.empty ());
}

void nano::request_aggregator::start ()
{
	debug_assert (threads.empty ());

	for (auto i = 0; i < config.threads; ++i)
	{
		threads.emplace_back ([this] () {
			nano::thread_role::set (nano::thread_role::name::request_aggregator);
			run ();
		});
	}
}

void nano::request_aggregator::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
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

std::size_t nano::request_aggregator::size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return queue.size ();
}

bool nano::request_aggregator::empty () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return queue.empty ();
}

bool nano::request_aggregator::request (request_type const & request, std::shared_ptr<nano::transport::channel> const & channel)
{
	release_assert (channel != nullptr);

	// This should be checked before calling request
	debug_assert (wallets.reps ().voting > 0);
	debug_assert (!request.empty ());

	bool added = false;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		added = queue.push ({ request, channel }, { nano::no_value{}, channel });
	}
	if (added)
	{
		stats.inc (nano::stat::type::request_aggregator, nano::stat::detail::request);
		stats.add (nano::stat::type::request_aggregator, nano::stat::detail::request_hashes, request.size ());

		condition.notify_one ();
	}
	else
	{
		stats.inc (nano::stat::type::request_aggregator, nano::stat::detail::overfill);
		stats.add (nano::stat::type::request_aggregator, nano::stat::detail::overfill_hashes, request.size ());
	}

	// TODO: This stat is for compatibility with existing tests and is in principle unnecessary
	stats.inc (nano::stat::type::aggregator, added ? nano::stat::detail::aggregator_accepted : nano::stat::detail::aggregator_dropped);

	return added;
}

void nano::request_aggregator::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::request_aggregator, nano::stat::detail::loop);

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

void nano::request_aggregator::run_batch (nano::unique_lock<nano::mutex> & lock)
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

		if (!channel->max ())
		{
			process (transaction, request, channel);
		}
		else
		{
			stats.inc (nano::stat::type::request_aggregator, nano::stat::detail::channel_full, stat::dir::out);
		}
	}
}

void nano::request_aggregator::process (nano::secure::transaction const & transaction, request_type const & request, std::shared_ptr<nano::transport::channel> const & channel)
{
	auto const remaining = aggregate (transaction, request, channel);

	if (!remaining.remaining_normal.empty ())
	{
		stats.inc (nano::stat::type::request_aggregator_replies, nano::stat::detail::normal_vote);

		// Generate votes for the remaining hashes
		auto const generated = generator.generate (remaining.remaining_normal, channel);
		stats.add (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote, stat::dir::in, remaining.remaining_normal.size () - generated);
	}
	if (!remaining.remaining_final.empty ())
	{
		stats.inc (nano::stat::type::request_aggregator_replies, nano::stat::detail::final_vote);

		// Generate final votes for the remaining hashes
		auto const generated = final_generator.generate (remaining.remaining_final, channel);
		stats.add (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote, stat::dir::in, remaining.remaining_final.size () - generated);
	}
}

void nano::request_aggregator::reply_action (std::shared_ptr<nano::vote> const & vote_a, std::shared_ptr<nano::transport::channel> const & channel_a) const
{
	nano::confirm_ack confirm{ network_constants, vote_a };
	channel_a->send (confirm);
}

void nano::request_aggregator::erase_duplicates (std::vector<std::pair<nano::block_hash, nano::root>> & requests_a) const
{
	std::sort (requests_a.begin (), requests_a.end (), [] (auto const & pair1, auto const & pair2) {
		return pair1.first < pair2.first;
	});
	requests_a.erase (std::unique (requests_a.begin (), requests_a.end (), [] (auto const & pair1, auto const & pair2) {
		return pair1.first == pair2.first;
	}),
	requests_a.end ());
}

auto nano::request_aggregator::aggregate (nano::secure::transaction const & transaction, request_type const & requests_a, std::shared_ptr<nano::transport::channel> const & channel_a) const -> aggregate_result
{
	std::vector<std::shared_ptr<nano::block>> to_generate;
	std::vector<std::shared_ptr<nano::block>> to_generate_final;
	for (auto const & [hash, root] : requests_a)
	{
		bool generate_final_vote (false);
		std::shared_ptr<nano::block> block;

		// 2. Final votes
		auto final_vote_hashes (ledger.store.final_vote.get (transaction, root));
		if (!final_vote_hashes.empty ())
		{
			generate_final_vote = true;
			block = ledger.any.block_get (transaction, final_vote_hashes[0]);
			// Allow same root vote
			if (block != nullptr && final_vote_hashes.size () > 1)
			{
				// WTF? This shouldn't be done like this
				to_generate_final.push_back (block);
				block = ledger.any.block_get (transaction, final_vote_hashes[1]);
				debug_assert (final_vote_hashes.size () == 2);
			}
		}

		// 4. Ledger by hash
		if (block == nullptr)
		{
			block = ledger.any.block_get (transaction, hash);
			// Confirmation status. Generate final votes for confirmed
			if (block != nullptr)
			{
				generate_final_vote = ledger.confirmed.block_exists (transaction, hash);
			}
		}

		// 5. Ledger by root
		if (block == nullptr && !root.is_zero ())
		{
			// Search for block root
			auto successor = ledger.any.block_successor (transaction, root.as_block_hash ());
			if (successor)
			{
				auto successor_block = ledger.any.block_get (transaction, successor.value ());
				release_assert (successor_block != nullptr);
				block = std::move (successor_block);

				// Confirmation status. Generate final votes for confirmed successor
				if (block != nullptr)
				{
					generate_final_vote = ledger.confirmed.block_exists (transaction, block->hash ());
				}
			}
		}

		if (block)
		{
			if (generate_final_vote)
			{
				to_generate_final.push_back (block);
				stats.inc (nano::stat::type::requests, nano::stat::detail::requests_final);
			}
			else
			{
				stats.inc (nano::stat::type::requests, nano::stat::detail::requests_non_final);
			}
		}
		else
		{
			stats.inc (nano::stat::type::requests, nano::stat::detail::requests_unknown);
		}
	}

	return {
		.remaining_normal = to_generate,
		.remaining_final = to_generate_final
	};
}

std::unique_ptr<nano::container_info_component> nano::request_aggregator::collect_container_info (std::string const & name) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (queue.collect_container_info ("queue"));
	return composite;
}

/*
 * request_aggregator_config
 */

nano::error nano::request_aggregator_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("max_queue", max_queue, "Maximum number of queued requests per peer. \ntype:uint64");
	toml.put ("threads", threads, "Number of threads for request processing. \ntype:uint64");
	toml.put ("batch_size", batch_size, "Number of requests to process in a single batch. \ntype:uint64");

	return toml.get_error ();
}

nano::error nano::request_aggregator_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("max_queue", max_queue);
	toml.get ("threads", threads);
	toml.get ("batch_size", batch_size);

	return toml.get_error ();
}
