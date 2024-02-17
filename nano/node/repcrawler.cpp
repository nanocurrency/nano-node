#include <nano/node/node.hpp>
#include <nano/node/repcrawler.hpp>

#include <boost/format.hpp>

nano::rep_crawler::rep_crawler (nano::node & node_a) :
	node{ node_a },
	stats{ node_a.stats },
	network_constants{ node_a.network_params.network },
	active{ node_a.active }
{
	if (!node.flags.disable_rep_crawler)
	{
		node.observers.endpoint.add ([this] (std::shared_ptr<nano::transport::channel> const & channel) {
			query (channel);
		});
	}
}

nano::rep_crawler::~rep_crawler ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::rep_crawler::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::rep_crawler);
		run ();
	} };
}

void nano::rep_crawler::stop ()
{
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

void nano::rep_crawler::remove (nano::block_hash const & hash)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	queries.erase (hash);
}

void nano::rep_crawler::validate_and_process (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (lock.owns_lock ());

	decltype (responses) responses_l;
	responses_l.swap (responses);

	lock.unlock ();

	// normally the rep_crawler only tracks principal reps but it can be made to track
	// reps with less weight by setting rep_crawler_weight_minimum to a low value
	auto const minimum = std::min (node.minimum_principal_weight (), node.config.rep_crawler_weight_minimum.number ());

	for (auto const & response : responses_l)
	{
		auto & vote = response.second;
		auto & channel = response.first;
		debug_assert (channel != nullptr);

		if (channel->get_type () == nano::transport::transport_type::loopback)
		{
			node.logger.debug (nano::log::type::repcrawler, "Ignoring vote from loopback channel: {}", channel->to_string ());
			continue;
		}

		nano::uint128_t const rep_weight = node.ledger.weight (vote->account);
		if (rep_weight < minimum)
		{
			node.logger.debug (nano::log::type::repcrawler, "Ignoring vote from account {} with too little voting weight: {}",
			vote->account.to_account (),
			nano::util::to_str (rep_weight));
			continue;
		}

		// temporary data used for logging after dropping the lock
		bool inserted = false;
		bool updated = false;
		std::shared_ptr<nano::transport::channel> prev_channel;

		lock.lock ();

		if (auto existing = reps.find (vote->account); existing != reps.end ())
		{
			reps.modify (existing, [rep_weight, &updated, &vote, &channel, &prev_channel] (representative_entry & info) {
				info.last_response = std::chrono::steady_clock::now ();

				// Update if representative channel was changed
				if (info.channel->get_endpoint () != channel->get_endpoint ())
				{
					debug_assert (info.account == vote->account);
					updated = true;
					prev_channel = info.channel;
					info.channel = channel;
				}
			});
		}
		else
		{
			reps.emplace (representative_entry{ vote->account, channel });
			inserted = true;
		}

		lock.unlock ();

		if (inserted)
		{
			node.logger.info (nano::log::type::repcrawler, "Found representative {} at {}", vote->account.to_account (), channel->to_string ());
		}
		if (updated)
		{
			node.logger.warn (nano::log::type::repcrawler, "Updated representative {} at {} (was at: {})", vote->account.to_account (), channel->to_string (), prev_channel->to_string ());
		}
	}
}

void nano::rep_crawler::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();

		auto const current_total_weight = total_weight ();
		bool const sufficient_weight = current_total_weight > node.online_reps.delta ();

		// If online weight drops below minimum, reach out to preconfigured peers
		if (!sufficient_weight)
		{
			stats.inc (nano::stat::type::rep_crawler, nano::stat::detail::keepalive);
			node.keepalive_preconfigured (node.config.preconfigured_peers);
		}

		lock.lock ();

		condition.wait_for (lock, sufficient_weight ? network_constants.rep_crawler_normal_interval : network_constants.rep_crawler_warmup_interval);
		if (stopped)
		{
			return;
		}

		stats.inc (nano::stat::type::rep_crawler, nano::stat::detail::loop);

		cleanup ();

		validate_and_process (lock);
		debug_assert (!lock.owns_lock ());

		auto targets = get_crawl_targets (current_total_weight);
		query (targets);

		lock.lock ();
	}
}

void nano::rep_crawler::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	erase_if (reps, [this] (representative_entry const & rep) {
		if (!rep.channel->alive ())
		{
			stats.inc (nano::stat::type::rep_crawler, nano::stat::detail::channel_dead);
			return true; // Erase
		}
		return false;
	});
}

std::vector<std::shared_ptr<nano::transport::channel>> nano::rep_crawler::get_crawl_targets (nano::uint128_t current_total_weight) const
{
	// TODO: Make these values configurable
	constexpr std::size_t conservative_count = 10;
	constexpr std::size_t aggressive_count = 40;

	// Crawl more aggressively if we lack sufficient total peer weight.
	bool sufficient_weight = current_total_weight > node.online_reps.delta ();
	auto required_peer_count = sufficient_weight ? conservative_count : aggressive_count;

	// Add random peers. We do this even if we have enough weight, in order to pick up reps
	// that didn't respond when first observed. If the current total weight isn't sufficient, this
	// will be more aggressive. When the node first starts, the rep container is empty and all
	// endpoints will originate from random peers.
	required_peer_count += required_peer_count / 2;

	// The rest of the endpoints are picked randomly
	auto random_peers = node.network.random_set (required_peer_count, 0, true); // Include channels with ephemeral remote ports
	return { random_peers.begin (), random_peers.end () };
}

std::optional<std::pair<nano::block_hash, nano::block_hash>> nano::rep_crawler::prepare_query_target ()
{
	constexpr int max_attempts = 4;

	auto transaction = node.store.tx_begin_read ();

	std::optional<std::pair<nano::block_hash, nano::block_hash>> hash_root;

	// Randomly select a block from ledger to request votes for
	for (auto i = 0; i < max_attempts && !hash_root; ++i)
	{
		hash_root = node.ledger.hash_root_random (transaction);

		// Rebroadcasted votes for recently confirmed blocks might confuse the rep crawler
		if (active.recently_confirmed.exists (hash_root->first))
		{
			hash_root = std::nullopt;
		}
	}

	if (!hash_root)
	{
		return std::nullopt;
	}

	// Don't send same block multiple times in tests
	if (node.network_params.network.is_dev_network ())
	{
		nano::lock_guard<nano::mutex> lock{ mutex };

		for (auto i = 0; queries.count (hash_root->first) != 0 && i < 4; ++i)
		{
			hash_root = node.ledger.hash_root_random (transaction);
		}
	}

	if (!hash_root)
	{
		return std::nullopt;
	}

	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		queries.insert (hash_root->first);
	}

	return hash_root;
}

void nano::rep_crawler::query (std::vector<std::shared_ptr<nano::transport::channel>> const & target_channels)
{
	auto maybe_hash_root = prepare_query_target ();
	if (!maybe_hash_root)
	{
		return;
	}
	auto hash_root = *maybe_hash_root;

	for (const auto & channel : target_channels)
	{
		debug_assert (channel != nullptr);
		on_rep_request (channel);
		node.network.send_confirm_req (channel, hash_root);
	}

	// TODO: Use a thread+timeout instead of a timer
	// A representative must respond with a vote within the deadline
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w, hash = hash_root.first] () {
		if (auto node_l = node_w.lock ())
		{
			auto target_finished_processed (node_l->vote_processor.total_processed + node_l->vote_processor.size ());
			node_l->rep_crawler.throttled_remove (hash, target_finished_processed);
		}
	});
}

void nano::rep_crawler::query (std::shared_ptr<nano::transport::channel> const & target_channel)
{
	query (std::vector{ target_channel });
}

// TODO: Use a thread+timeout instead of a timer
void nano::rep_crawler::throttled_remove (nano::block_hash const & hash_a, uint64_t const target_finished_processed)
{
	if (node.vote_processor.total_processed >= target_finished_processed)
	{
		remove (hash_a);
	}
	else
	{
		std::weak_ptr<nano::node> node_w (node.shared ());
		node.workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w, hash_a, target_finished_processed] () {
			if (auto node_l = node_w.lock ())
			{
				node_l->rep_crawler.throttled_remove (hash_a, target_finished_processed);
			}
		});
	}
}

bool nano::rep_crawler::is_pr (nano::transport::channel const & channel_a) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto existing = reps.get<tag_channel_ref> ().find (channel_a);
	if (existing != reps.get<tag_channel_ref> ().end ())
	{
		return node.ledger.weight (existing->account) > node.minimum_principal_weight ();
	}
	return false;
}

// TODO: Remove force parameter
bool nano::rep_crawler::response (std::shared_ptr<nano::transport::channel> const & channel, std::shared_ptr<nano::vote> const & vote, bool force)
{
	bool error = true;
	nano::lock_guard<nano::mutex> lock{ mutex };
	for (auto const & hash : vote->hashes)
	{
		if (force || queries.count (hash) != 0)
		{
			responses.emplace_back (channel, vote);
			error = false;
			break;
		}
	}
	return error;
}

nano::uint128_t nano::rep_crawler::total_weight () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	nano::uint128_t result = 0;
	for (const auto & rep : reps)
	{
		if (rep.channel->alive ())
		{
			result += node.ledger.weight (rep.account);
		}
	}
	return result;
}

void nano::rep_crawler::on_rep_request (std::shared_ptr<nano::transport::channel> const & channel_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	if (channel_a->get_tcp_endpoint ().address () != boost::asio::ip::address_v6::any ())
	{
		auto & channel_ref_index = reps.get<tag_channel_ref> ();

		// Find and update the timestamp on all reps available on the endpoint (a single host may have multiple reps)
		auto itr_pair = channel_ref_index.equal_range (*channel_a);
		for (; itr_pair.first != itr_pair.second; itr_pair.first++)
		{
			channel_ref_index.modify (itr_pair.first, [] (representative_entry & info) {
				info.last_request = std::chrono::steady_clock::now ();
			});
		}
	}
}

std::vector<nano::representative> nano::rep_crawler::representatives (std::size_t count_a, nano::uint128_t const weight_a, boost::optional<decltype (nano::network_constants::protocol_version)> const & opt_version_min_a)
{
	auto const version_min = opt_version_min_a.value_or (node.network_params.network.protocol_version_min);

	nano::lock_guard<nano::mutex> lock{ mutex };

	std::multimap<nano::amount, representative_entry, std::greater<>> ordered;
	for (const auto & i : reps.get<tag_account> ())
	{
		auto weight = node.ledger.weight (i.account);
		if (weight > weight_a && i.channel->get_network_version () >= version_min)
		{
			ordered.insert ({ nano::amount{ weight }, i });
		}
	}

	std::vector<nano::representative> result;
	for (auto i = ordered.begin (), n = ordered.end (); i != n && result.size () < count_a; ++i)
	{
		result.push_back ({ i->second.account, i->second.channel });
	}
	return result;
}

std::vector<nano::representative> nano::rep_crawler::principal_representatives (std::size_t count_a, boost::optional<decltype (nano::network_constants::protocol_version)> const & opt_version_min_a)
{
	return representatives (count_a, node.minimum_principal_weight (), opt_version_min_a);
}

/** Total number of representatives */
std::size_t nano::rep_crawler::representative_count ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return reps.size ();
}

// Only for tests
void nano::rep_crawler::force_add_rep (const nano::account & account, const std::shared_ptr<nano::transport::channel> & channel)
{
	release_assert (node.network_params.network.is_dev_network ());
	nano::lock_guard<nano::mutex> lock{ mutex };
	reps.emplace (representative_entry{ account, channel });
}

// Only for tests
void nano::rep_crawler::force_response (const std::shared_ptr<nano::transport::channel> & channel, const std::shared_ptr<nano::vote> & vote)
{
	release_assert (node.network_params.network.is_dev_network ());
	nano::lock_guard<nano::mutex> lock{ mutex };
	for (auto const & hash : vote->hashes)
	{
		responses.emplace_back (channel, vote);
	}
}

// Only for tests
void nano::rep_crawler::force_active_query (const nano::block_hash & hash)
{
	release_assert (node.network_params.network.is_dev_network ());
	nano::lock_guard<nano::mutex> lock{ mutex };
	queries.insert (hash);
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (rep_crawler & rep_crawler, std::string const & name)
{
	std::size_t count;
	{
		nano::lock_guard<nano::mutex> guard{ rep_crawler.mutex };
		count = rep_crawler.queries.size ();
	}

	auto const sizeof_element = sizeof (decltype (rep_crawler.queries)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "queries", count, sizeof_element }));
	return composite;
}