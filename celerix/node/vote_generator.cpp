#include <celerix/lib/blocks.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/local_vote_history.hpp>
#include <celerix/node/network.hpp>
#include <celerix/node/nodeconfig.hpp>
#include <celerix/node/transport/inproc.hpp>
#include <celerix/node/vote_generator.hpp>
#include <celerix/node/vote_processor.hpp>
#include <celerix/node/vote_spacing.hpp>
#include <celerix/node/wallet.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/secure/vote.hpp>
#include <celerix/store/component.hpp>

#include <chrono>

celerix::vote_generator::vote_generator (celerix::node_config const & config_a, celerix::node & node_a, celerix::ledger & ledger_a, celerix::wallets & wallets_a, celerix::vote_processor & vote_processor_a, celerix::local_vote_history & history_a, celerix::network & network_a, celerix::stats & stats_a, celerix::logger & logger_a, bool is_final_a) :
	config (config_a),
	node (node_a),
	ledger (ledger_a),
	wallets (wallets_a),
	vote_processor (vote_processor_a),
	history (history_a),
	spacing_impl{ std::make_unique<celerix::vote_spacing> (config_a.network_params.voting.delay) },
	spacing{ *spacing_impl },
	network (network_a),
	stats (stats_a),
	logger (logger_a),
	is_final (is_final_a),
	vote_generation_queue{ stats, celerix::stat::type::vote_generator, celerix::thread_role::name::vote_generator_queue, /* single threaded */ 1, /* max queue size */ 1024 * 32, /* max batch size */ 256 },
	inproc_channel{ std::make_shared<celerix::transport::inproc::channel> (node, node) }
{
	vote_generation_queue.process_batch = [this] (auto & batch) {
		process_batch (batch);
	};
}

celerix::vote_generator::~vote_generator ()
{
	debug_assert (stopped);
	debug_assert (!thread.joinable ());
}

bool celerix::vote_generator::should_vote (transaction_variant_t const & transaction_variant, celerix::root const & root_a, celerix::block_hash const & hash_a) const
{
	bool should_vote = false;
	std::shared_ptr<celerix::block> block;
	if (is_final)
	{
		debug_assert (std::holds_alternative<celerix::secure::write_transaction> (transaction_variant));
		auto const & transaction = std::get<celerix::secure::write_transaction> (transaction_variant);

		block = ledger.any.block_get (transaction, hash_a);
		should_vote = block != nullptr && ledger.dependents_confirmed (transaction, *block) && ledger.store.final_vote.put (transaction, block->qualified_root (), hash_a);
		debug_assert (block == nullptr || root_a == block->root ());
	}
	else
	{
		debug_assert (std::holds_alternative<celerix::secure::read_transaction> (transaction_variant));
		auto const & transaction = std::get<celerix::secure::read_transaction> (transaction_variant);

		block = ledger.any.block_get (transaction, hash_a);
		should_vote = block != nullptr && ledger.dependents_confirmed (transaction, *block);
	}

	logger.trace (celerix::log::type::vote_generator, celerix::log::detail::should_vote,
	celerix::log::arg{ "should_vote", should_vote },
	celerix::log::arg{ "block", block },
	celerix::log::arg{ "is_final", is_final });

	return should_vote;
}

void celerix::vote_generator::start ()
{
	debug_assert (!thread.joinable ());
	thread = std::thread ([this] () {
		celerix::thread_role::set (celerix::thread_role::name::voting);
		run ();
	});
	vote_generation_queue.start ();
}

void celerix::vote_generator::stop ()
{
	vote_generation_queue.stop ();
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

void celerix::vote_generator::add (const root & root, const block_hash & hash)
{
	vote_generation_queue.add (std::make_pair (root, hash));
}

void celerix::vote_generator::process_batch (std::deque<queue_entry_t> & batch)
{
	std::deque<candidate_t> verified;

	auto refresh_if_needed = [] (auto && transaction_variant) {
		std::visit ([&] (auto && transaction) { transaction.refresh_if_needed (); }, transaction_variant);
	};

	auto verify_batch = [this, &verified, &refresh_if_needed] (auto && transaction_variant, auto && batch) {
		for (auto & [root, hash] : batch)
		{
			refresh_if_needed (transaction_variant);

			if (should_vote (transaction_variant, root, hash))
			{
				verified.emplace_back (root, hash);
			}
		}
	};

	if (is_final)
	{
		transaction_variant_t transaction_variant{ ledger.tx_begin_write (celerix::store::writer::voting_final) };
		verify_batch (transaction_variant, batch);
		// Commit write transaction
	}
	else
	{
		transaction_variant_t transaction_variant{ ledger.tx_begin_read () };
		verify_batch (transaction_variant, batch);
	}

	// Submit verified candidates to the main processing thread
	if (!verified.empty ())
	{
		celerix::unique_lock<celerix::mutex> lock{ mutex };
		candidates.insert (candidates.end (), verified.begin (), verified.end ());
		if (candidates.size () >= celerix::network::confirm_ack_hashes_max)
		{
			lock.unlock ();
			condition.notify_all ();
		}
	}
}

std::size_t celerix::vote_generator::generate (std::vector<std::shared_ptr<celerix::block>> const & blocks_a, std::shared_ptr<celerix::transport::channel> const & channel_a)
{
	request_t::first_type req_candidates;
	{
		auto transaction = ledger.tx_begin_read ();
		auto dependents_confirmed = [&transaction, this] (auto const & block_a) {
			return this->ledger.dependents_confirmed (transaction, *block_a);
		};
		auto as_candidate = [] (auto const & block_a) {
			return candidate_t{ block_a->root (), block_a->hash () };
		};
		celerix::transform_if (blocks_a.begin (), blocks_a.end (), std::back_inserter (req_candidates), dependents_confirmed, as_candidate);
	}
	auto const result = req_candidates.size ();
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	requests.emplace_back (std::move (req_candidates), channel_a);
	while (requests.size () > max_requests)
	{
		// On a large queue of requests, erase the oldest one
		requests.pop_front ();
		stats.inc (celerix::stat::type::vote_generator, celerix::stat::detail::generator_replies_discarded);
	}
	return result;
}

void celerix::vote_generator::broadcast (celerix::unique_lock<celerix::mutex> & lock_a)
{
	debug_assert (lock_a.owns_lock ());

	std::vector<celerix::block_hash> hashes;
	std::vector<celerix::root> roots;
	hashes.reserve (celerix::network::confirm_ack_hashes_max);
	roots.reserve (celerix::network::confirm_ack_hashes_max);
	while (!candidates.empty () && hashes.size () < celerix::network::confirm_ack_hashes_max)
	{
		auto const & [root, hash] = candidates.front ();
		if (std::find (roots.begin (), roots.end (), root) == roots.end ())
		{
			if (spacing.votable (root, hash))
			{
				roots.push_back (root);
				hashes.push_back (hash);
			}
			else
			{
				stats.inc (celerix::stat::type::vote_generator, celerix::stat::detail::generator_spacing);
			}
		}
		candidates.pop_front ();
	}
	if (!hashes.empty ())
	{
		lock_a.unlock ();
		vote (hashes, roots, [this] (auto const & generated_vote) {
			stats.inc (celerix::stat::type::vote_generator, celerix::stat::detail::generator_broadcasts);
			stats.sample (is_final ? celerix::stat::sample::vote_generator_final_hashes : celerix::stat::sample::vote_generator_hashes, generated_vote->hashes.size (), { 0, celerix::network::confirm_ack_hashes_max });
			broadcast_action (generated_vote);
		});
		lock_a.lock ();
	}
}

void celerix::vote_generator::reply (celerix::unique_lock<celerix::mutex> & lock_a, request_t && request_a)
{
	if (request_a.second->max (celerix::transport::traffic_type::vote_reply))
	{
		return;
	}
	lock_a.unlock ();
	auto i (request_a.first.cbegin ());
	auto n (request_a.first.cend ());
	while (i != n && !stopped)
	{
		std::vector<celerix::block_hash> hashes;
		std::vector<celerix::root> roots;
		hashes.reserve (celerix::network::confirm_ack_hashes_max);
		roots.reserve (celerix::network::confirm_ack_hashes_max);
		for (; i != n && hashes.size () < celerix::network::confirm_ack_hashes_max; ++i)
		{
			auto const & [root, hash] = *i;
			if (std::find (roots.begin (), roots.end (), root) == roots.end ())
			{
				if (spacing.votable (root, hash))
				{
					roots.push_back (root);
					hashes.push_back (hash);
				}
				else
				{
					stats.inc (celerix::stat::type::vote_generator, celerix::stat::detail::generator_spacing);
				}
			}
		}
		if (!hashes.empty ())
		{
			stats.add (celerix::stat::type::requests, celerix::stat::detail::requests_generated_hashes, stat::dir::in, hashes.size ());

			vote (hashes, roots, [this, channel = request_a.second] (std::shared_ptr<celerix::vote> const & vote_a) {
				celerix::confirm_ack confirm{ config.network_params.network, vote_a };
				channel->send (confirm, celerix::transport::traffic_type::vote_reply);
				stats.inc (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes, stat::dir::in);
			});
		}
	}
	stats.inc (celerix::stat::type::vote_generator, celerix::stat::detail::generator_replies);
	lock_a.lock ();
}

void celerix::vote_generator::vote (std::vector<celerix::block_hash> const & hashes_a, std::vector<celerix::root> const & roots_a, std::function<void (std::shared_ptr<celerix::vote> const &)> const & action_a)
{
	debug_assert (hashes_a.size () == roots_a.size ());
	std::vector<std::shared_ptr<celerix::vote>> votes_l;
	wallets.foreach_representative ([this, &hashes_a, &votes_l] (celerix::public_key const & pub_a, celerix::raw_key const & prv_a) {
		auto timestamp = this->is_final ? celerix::vote::timestamp_max : celerix::milliseconds_since_epoch ();
		uint8_t duration = this->is_final ? celerix::vote::duration_max : /*8192ms*/ 0x9;
		votes_l.emplace_back (std::make_shared<celerix::vote> (pub_a, prv_a, timestamp, duration, hashes_a));
	});
	for (auto const & vote_l : votes_l)
	{
		for (std::size_t i (0), n (hashes_a.size ()); i != n; ++i)
		{
			history.add (roots_a[i], hashes_a[i], vote_l);
			spacing.flag (roots_a[i], hashes_a[i]);
		}
		action_a (vote_l);
	}
}

void celerix::vote_generator::broadcast_action (std::shared_ptr<celerix::vote> const & vote_a) const
{
	network.flood_vote_pr (vote_a);
	network.flood_vote_non_pr (vote_a, 2.0f);
	vote_processor.vote (vote_a, inproc_channel);
}

void celerix::vote_generator::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, config.vote_generator_delay, [this] () {
			return stopped || broadcast_predicate () || !requests.empty ();
		});

		if (stopped)
		{
			return;
		}

		if (broadcast_predicate ())
		{
			broadcast (lock);
			next_broadcast = std::chrono::steady_clock::now () + config.vote_generator_delay;
		}

		if (!requests.empty ())
		{
			auto request (requests.front ());
			requests.pop_front ();
			reply (lock, std::move (request));
		}
	}
}

bool celerix::vote_generator::broadcast_predicate () const
{
	debug_assert (!mutex.try_lock ());

	if (candidates.size () >= celerix::network::confirm_ack_hashes_max)
	{
		return true;
	}
	if (!candidates.empty () && std::chrono::steady_clock::now () > next_broadcast)
	{
		return true;
	}
	return false;
}

celerix::container_info celerix::vote_generator::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("candidates", candidates.size ());
	info.put ("requests", requests.size ());
	info.add ("queue", vote_generation_queue.container_info ());
	return info;
}
