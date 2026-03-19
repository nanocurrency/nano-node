#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/local_vote_history.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/node/vote_generator.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/vote_spacing.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/voting_policy.hpp>

#include <chrono>

nano::vote_generator::vote_generator (vote_generator_config const & config_a, nano::node & node_a, nano::voting_policy & policy_a, nano::ledger & ledger_a, nano::wallets & wallets_a, nano::vote_processor & vote_processor_a, nano::local_vote_history & history_a, nano::network & network_a, nano::stats & stats_a, nano::logger & logger_a, bool is_final_a, std::shared_ptr<nano::transport::channel> inproc_channel_a) :
	config (config_a),
	node (node_a),
	policy (policy_a),
	ledger (ledger_a),
	wallets (wallets_a),
	vote_processor (vote_processor_a),
	history (history_a),
	spacing_impl{ std::make_unique<nano::vote_spacing> (node_a.network_params.voting.delay) },
	spacing{ *spacing_impl },
	network (network_a),
	stats (stats_a),
	logger (logger_a),
	is_final (is_final_a),
	inproc_channel{ inproc_channel_a },
	vote_generation_queue{ stats, nano::stat::type::vote_generator, is_final ? nano::thread_role::name::voting_final : nano::thread_role::name::voting, /* single threaded */ 1, config.max_queue, config.batch_size }
{
	vote_generation_queue.process_batch = [this] (auto & batch) {
		process_batch (batch);
	};
}

nano::vote_generator::~vote_generator ()
{
	debug_assert (stopped);
	debug_assert (!thread.joinable ());
}

void nano::vote_generator::start ()
{
	debug_assert (!thread.joinable ());
	thread = std::thread ([this] () {
		nano::thread_role::set (is_final ? nano::thread_role::name::voting_final : nano::thread_role::name::voting);
		run ();
	});
	vote_generation_queue.start ();
}

void nano::vote_generator::stop ()
{
	vote_generation_queue.stop ();
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	if (thread.joinable ())
	{
		thread.join ();
	}
}

void nano::vote_generator::add (const root & root, const block_hash & hash)
{
	vote_generation_queue.add (std::make_pair (root, hash));
}

void nano::vote_generator::process_batch (std::deque<queue_entry_t> & batch)
{
	std::deque<nano::vote_permit> verified;

	if (is_final)
	{
		auto transaction = ledger.tx_begin_write (nano::store::writer::voting_final);
		for (auto & [root, hash] : batch)
		{
			transaction.refresh_if_needed ();
			auto block = ledger.any.block_get (transaction, hash);
			if (block)
			{
				if (auto permit = policy.vote_final (transaction, *block))
				{
					verified.push_back (*permit);
				}
			}
		}
		// Commit write transaction
	}
	else
	{
		auto transaction = ledger.tx_begin_read ();
		for (auto & [root, hash] : batch)
		{
			transaction.refresh_if_needed ();
			auto block = ledger.any.block_get (transaction, hash);
			if (block)
			{
				if (auto permit = policy.vote_normal (transaction, *block))
				{
					verified.push_back (*permit);
				}
			}
		}
	}

	// Submit verified candidates to the main processing thread
	if (!verified.empty ())
	{
		nano::unique_lock<nano::mutex> lock{ mutex };
		candidates.insert (candidates.end (), verified.begin (), verified.end ());
		if (candidates.size () >= nano::network::confirm_ack_hashes_max)
		{
			lock.unlock ();
			condition.notify_all ();
		}
	}
}

std::size_t nano::vote_generator::generate (std::vector<std::shared_ptr<nano::block>> const & blocks_a, std::shared_ptr<nano::transport::channel> const & channel_a)
{
	request_t::first_type req_candidates;
	{
		auto transaction = ledger.tx_begin_read ();
		for (auto const & block : blocks_a)
		{
			auto permit = is_final
			? policy.reply_final (transaction, *block)
			: policy.vote_normal (transaction, *block);
			if (permit)
			{
				req_candidates.push_back (*permit);
			}
		}
	}
	auto const result = req_candidates.size ();
	nano::lock_guard<nano::mutex> guard{ mutex };
	requests.emplace_back (std::move (req_candidates), channel_a);
	while (requests.size () > max_requests)
	{
		// On a large queue of requests, erase the oldest one
		requests.pop_front ();
		stats.inc (stat_type (), nano::stat::detail::generator_replies_discarded);
	}
	return result;
}

void nano::vote_generator::broadcast (nano::unique_lock<nano::mutex> & lock_a)
{
	debug_assert (lock_a.owns_lock ());

	std::vector<nano::vote_permit> permits;
	permits.reserve (nano::network::confirm_ack_hashes_max);
	std::vector<nano::root> seen_roots;
	seen_roots.reserve (nano::network::confirm_ack_hashes_max);
	while (!candidates.empty () && permits.size () < nano::network::confirm_ack_hashes_max)
	{
		auto permit = std::move (candidates.front ());
		candidates.pop_front ();
		if (std::find (seen_roots.begin (), seen_roots.end (), permit.root ()) == seen_roots.end ())
		{
			if (spacing.votable (permit.root (), permit.hash ()))
			{
				seen_roots.push_back (permit.root ());
				permits.push_back (std::move (permit));
			}
			else
			{
				stats.inc (stat_type (), nano::stat::detail::generator_spacing);
			}
		}
	}
	if (!permits.empty ())
	{
		lock_a.unlock ();
		auto signer = [this] (auto const & callback) { wallets.foreach_representative (callback); };
		auto votes = policy.sign (is_final ? nano::vote_type::final : nano::vote_type::normal, permits, signer);
		for (auto const & vote : votes)
		{
			for (auto const & permit : permits)
			{
				history.add (permit.root (), permit.hash (), vote);
				spacing.flag (permit.root (), permit.hash ());
			}
			stats.inc (stat_type (), nano::stat::detail::generator_broadcasts);
			stats.sample (is_final ? nano::stat::sample::vote_generator_final_hashes : nano::stat::sample::vote_generator_hashes, vote->hashes.size (), { 0, nano::network::confirm_ack_hashes_max });
			broadcast_action (vote);
		}
		lock_a.lock ();
	}
}

void nano::vote_generator::reply (nano::unique_lock<nano::mutex> & lock_a, request_t && request_a)
{
	if (request_a.second->max (nano::transport::traffic_type::vote_reply))
	{
		return;
	}
	lock_a.unlock ();
	auto i (request_a.first.cbegin ());
	auto n (request_a.first.cend ());
	while (i != n && !stopped)
	{
		std::vector<nano::vote_permit> permits;
		permits.reserve (nano::network::confirm_ack_hashes_max);
		std::vector<nano::root> seen_roots;
		seen_roots.reserve (nano::network::confirm_ack_hashes_max);
		for (; i != n && permits.size () < nano::network::confirm_ack_hashes_max; ++i)
		{
			auto const & permit = *i;
			if (std::find (seen_roots.begin (), seen_roots.end (), permit.root ()) == seen_roots.end ())
			{
				if (spacing.votable (permit.root (), permit.hash ()))
				{
					seen_roots.push_back (permit.root ());
					permits.push_back (permit);
				}
				else
				{
					stats.inc (stat_type (), nano::stat::detail::generator_spacing);
				}
			}
		}
		if (!permits.empty ())
		{
			stats.add (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes, stat::dir::in, permits.size ());

			auto signer = [this] (auto const & callback) { wallets.foreach_representative (callback); };
			auto votes = policy.sign (is_final ? nano::vote_type::final : nano::vote_type::normal, permits, signer);
			for (auto const & vote : votes)
			{
				for (auto const & permit : permits)
				{
					history.add (permit.root (), permit.hash (), vote);
					spacing.flag (permit.root (), permit.hash ());
				}
				nano::messages::confirm_ack confirm{ node.network_params.network, vote };
				request_a.second->send (confirm, nano::transport::traffic_type::vote_reply);
				stats.inc (nano::stat::type::requests, nano::stat::detail::requests_generated_votes, stat::dir::in);
			}
		}
	}
	stats.inc (stat_type (), nano::stat::detail::generator_replies);
	lock_a.lock ();
}

void nano::vote_generator::broadcast_action (std::shared_ptr<nano::vote> const & vote_a) const
{
	vote_processor.vote (vote_a, inproc_channel);

	auto sent_pr = network.flood_vote_pr (vote_a);
	auto sent_non_pr = network.flood_vote_non_pr (vote_a, 2.0f);

	stats.add (stat_type (), nano::stat::detail::sent_pr, sent_pr);
	stats.add (stat_type (), nano::stat::detail::sent_non_pr, sent_non_pr);
}

void nano::vote_generator::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		// Wait for at most delay in case no further notification is received
		condition.wait_for (lock, config.delay, [this] () {
			return stopped || broadcast_predicate () || !requests.empty ();
		});

		if (stopped)
		{
			return;
		}

		if (broadcast_predicate () || !requests.empty ())
		{
			stats.inc (stat_type (), nano::stat::detail::loop);

			// Only log if component is under pressure
			if ((candidates.size () + requests.size ()) > nano::queue_warning_threshold () && log_interval.elapse (15s))
			{
				logger.info (log_type (), "{} candidates, {} requests in processing queue", candidates.size (), requests.size ());
			}

			if (broadcast_predicate ())
			{
				broadcast (lock);
				next_broadcast = std::chrono::steady_clock::now () + config.delay;
			}

			if (!requests.empty ())
			{
				auto request (requests.front ());
				requests.pop_front ();
				reply (lock, std::move (request));
			}
		}
	}
}

bool nano::vote_generator::broadcast_predicate () const
{
	debug_assert (!mutex.try_lock ());

	if (candidates.size () >= nano::network::confirm_ack_hashes_max)
	{
		return true;
	}
	if (!candidates.empty () && std::chrono::steady_clock::now () > next_broadcast)
	{
		return true;
	}
	return false;
}

nano::container_info nano::vote_generator::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("candidates", candidates.size ());
	info.put ("requests", requests.size ());
	info.add ("queue", vote_generation_queue.container_info ());
	return info;
}

nano::stat::type nano::vote_generator::stat_type () const
{
	return is_final ? nano::stat::type::vote_generator_final : nano::stat::type::vote_generator;
}

nano::log::type nano::vote_generator::log_type () const
{
	return is_final ? nano::log::type::vote_generator_final : nano::log::type::vote_generator;
}

/*
 * vote_generator_config
 */

nano::error nano::vote_generator_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("max_queue", max_queue, "Maximum number of entries in the vote generation queue. \ntype:uint64");
	toml.put ("batch_size", batch_size, "Maximum number of entries to process in a single batch. \ntype:uint64");
	toml.put ("delay", delay.count (), "Delay before votes are sent to allow for efficient bundling of hashes in votes. \ntype:milliseconds");

	return toml.get_error ();
}

nano::error nano::vote_generator_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("max_queue", max_queue);
	toml.get ("batch_size", batch_size);
	toml.get_duration ("delay", delay);

	return toml.get_error ();
}
