#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/local_vote_history.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/node/vote_generator.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/vote_spacing.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/component.hpp>

#include <chrono>

nano::vote_generator::vote_generator (nano::node_config const & config_a, nano::node & node_a, nano::ledger & ledger_a, nano::wallets & wallets_a, nano::vote_processor & vote_processor_a, nano::local_vote_history & history_a, nano::network & network_a, nano::stats & stats_a, nano::logger & logger_a, bool is_final_a) :
	config (config_a),
	node (node_a),
	ledger (ledger_a),
	wallets (wallets_a),
	vote_processor (vote_processor_a),
	history (history_a),
	spacing_impl{ std::make_unique<nano::vote_spacing> (config_a.network_params.voting.delay) },
	spacing{ *spacing_impl },
	network (network_a),
	stats (stats_a),
	logger (logger_a),
	is_final (is_final_a),
	vote_generation_queue{ stats, nano::stat::type::vote_generator, nano::thread_role::name::vote_generator_queue, /* single threaded */ 1, /* max queue size */ 1024 * 32, /* max batch size */ 1024 * 4 }
{
	vote_generation_queue.process_batch = [this] (auto & batch) {
		process_batch (batch);
	};
}

nano::vote_generator::~vote_generator ()
{
	stop ();
}

bool nano::vote_generator::should_vote (secure::write_transaction const & transaction, nano::root const & root_a, nano::block_hash const & hash_a)
{
	auto block = ledger.block (transaction, hash_a);
	bool should_vote = false;
	if (is_final)
	{
		should_vote = block != nullptr && ledger.dependents_confirmed (transaction, *block) && ledger.store.final_vote.put (transaction, block->qualified_root (), hash_a);
		debug_assert (block == nullptr || root_a == block->root ());
	}
	else
	{
		should_vote = block != nullptr && ledger.dependents_confirmed (transaction, *block);
	}

	logger.trace (nano::log::type::vote_generator, nano::log::detail::should_vote,
	nano::log::arg{ "should_vote", should_vote },
	nano::log::arg{ "block", block },
	nano::log::arg{ "is_final", is_final });

	return should_vote;
}

void nano::vote_generator::start ()
{
	debug_assert (!thread.joinable ());
	thread = std::thread ([this] () { run (); });

	vote_generation_queue.start ();
}

void nano::vote_generator::stop ()
{
	vote_generation_queue.stop ();

	nano::unique_lock<nano::mutex> lock{ mutex };
	stopped = true;

	lock.unlock ();
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
	std::deque<candidate_t> candidates_new;
	{
		auto guard = ledger.store.write_queue.wait (is_final ? nano::store::writer::voting_final : nano::store::writer::voting);
		auto transaction = ledger.tx_begin_write ({ tables::final_votes });

		for (auto & [root, hash] : batch)
		{
			if (should_vote (transaction, root, hash))
			{
				candidates_new.emplace_back (root, hash);
			}
		}
		// Commit write transaction
	}
	if (!candidates_new.empty ())
	{
		nano::unique_lock<nano::mutex> lock{ mutex };
		candidates.insert (candidates.end (), candidates_new.begin (), candidates_new.end ());
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
		auto dependents_confirmed = [&transaction, this] (auto const & block_a) {
			return this->ledger.dependents_confirmed (transaction, *block_a);
		};
		auto as_candidate = [] (auto const & block_a) {
			return candidate_t{ block_a->root (), block_a->hash () };
		};
		nano::transform_if (blocks_a.begin (), blocks_a.end (), std::back_inserter (req_candidates), dependents_confirmed, as_candidate);
	}
	auto const result = req_candidates.size ();
	nano::lock_guard<nano::mutex> guard{ mutex };
	requests.emplace_back (std::move (req_candidates), channel_a);
	while (requests.size () > max_requests)
	{
		// On a large queue of requests, erase the oldest one
		requests.pop_front ();
		stats.inc (nano::stat::type::vote_generator, nano::stat::detail::generator_replies_discarded);
	}
	return result;
}

void nano::vote_generator::set_reply_action (std::function<void (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> const &)> action_a)
{
	release_assert (!reply_action);
	reply_action = action_a;
}

void nano::vote_generator::broadcast (nano::unique_lock<nano::mutex> & lock_a)
{
	debug_assert (lock_a.owns_lock ());

	std::vector<nano::block_hash> hashes;
	std::vector<nano::root> roots;
	hashes.reserve (nano::network::confirm_ack_hashes_max);
	roots.reserve (nano::network::confirm_ack_hashes_max);
	while (!candidates.empty () && hashes.size () < nano::network::confirm_ack_hashes_max)
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
				stats.inc (nano::stat::type::vote_generator, nano::stat::detail::generator_spacing);
			}
		}
		candidates.pop_front ();
	}
	if (!hashes.empty ())
	{
		lock_a.unlock ();
		vote (hashes, roots, [this] (auto const & vote_a) {
			this->broadcast_action (vote_a);
			this->stats.inc (nano::stat::type::vote_generator, nano::stat::detail::generator_broadcasts);
		});
		lock_a.lock ();
	}
}

void nano::vote_generator::reply (nano::unique_lock<nano::mutex> & lock_a, request_t && request_a)
{
	lock_a.unlock ();
	auto i (request_a.first.cbegin ());
	auto n (request_a.first.cend ());
	while (i != n && !stopped)
	{
		std::vector<nano::block_hash> hashes;
		std::vector<nano::root> roots;
		hashes.reserve (nano::network::confirm_ack_hashes_max);
		roots.reserve (nano::network::confirm_ack_hashes_max);
		for (; i != n && hashes.size () < nano::network::confirm_ack_hashes_max; ++i)
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
					stats.inc (nano::stat::type::vote_generator, nano::stat::detail::generator_spacing);
				}
			}
		}
		if (!hashes.empty ())
		{
			stats.add (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes, stat::dir::in, hashes.size ());
			vote (hashes, roots, [this, &channel = request_a.second] (std::shared_ptr<nano::vote> const & vote_a) {
				this->reply_action (vote_a, channel);
				this->stats.inc (nano::stat::type::requests, nano::stat::detail::requests_generated_votes, stat::dir::in);
			});
		}
	}
	stats.inc (nano::stat::type::vote_generator, nano::stat::detail::generator_replies);
	lock_a.lock ();
}

void nano::vote_generator::vote (std::vector<nano::block_hash> const & hashes_a, std::vector<nano::root> const & roots_a, std::function<void (std::shared_ptr<nano::vote> const &)> const & action_a)
{
	debug_assert (hashes_a.size () == roots_a.size ());
	std::vector<std::shared_ptr<nano::vote>> votes_l;
	wallets.foreach_representative ([this, &hashes_a, &votes_l] (nano::public_key const & pub_a, nano::raw_key const & prv_a) {
		auto timestamp = this->is_final ? nano::vote::timestamp_max : nano::milliseconds_since_epoch ();
		uint8_t duration = this->is_final ? nano::vote::duration_max : /*8192ms*/ 0x9;
		votes_l.emplace_back (std::make_shared<nano::vote> (pub_a, prv_a, timestamp, duration, hashes_a));
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

void nano::vote_generator::broadcast_action (std::shared_ptr<nano::vote> const & vote_a) const
{
	network.flood_vote_pr (vote_a);
	network.flood_vote (vote_a, 2.0f);
	vote_processor.vote (vote_a, std::make_shared<nano::transport::inproc::channel> (node, node)); // TODO: Avoid creating a temporary channel each time
}

void nano::vote_generator::run ()
{
	nano::thread_role::set (nano::thread_role::name::voting);
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		if (candidates.size () >= nano::network::confirm_ack_hashes_max)
		{
			broadcast (lock);
		}
		else if (!requests.empty ())
		{
			auto request (requests.front ());
			requests.pop_front ();
			reply (lock, std::move (request));
		}
		else
		{
			condition.wait_for (lock, config.vote_generator_delay, [this] () { return this->candidates.size () >= nano::network::confirm_ack_hashes_max; });
			if (candidates.size () >= config.vote_generator_threshold && candidates.size () < nano::network::confirm_ack_hashes_max)
			{
				condition.wait_for (lock, config.vote_generator_delay, [this] () { return this->candidates.size () >= nano::network::confirm_ack_hashes_max; });
			}
			if (!candidates.empty ())
			{
				broadcast (lock);
			}
		}
	}
}

std::unique_ptr<nano::container_info_component> nano::vote_generator::collect_container_info (std::string const & name) const
{
	std::size_t candidates_count = 0;
	std::size_t requests_count = 0;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		candidates_count = candidates.size ();
		requests_count = requests.size ();
	}
	auto sizeof_candidate_element = sizeof (decltype (candidates)::value_type);
	auto sizeof_request_element = sizeof (decltype (requests)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "candidates", candidates_count, sizeof_candidate_element }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "requests", requests_count, sizeof_request_element }));
	composite->add_component (vote_generation_queue.collect_container_info ("vote_generation_queue"));
	return composite;
}
