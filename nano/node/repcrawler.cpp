#include <nano/node/node.hpp>
#include <nano/node/repcrawler.hpp>

#include <boost/format.hpp>

nano::rep_crawler::rep_crawler (nano::rep_crawler_config const & config_a, nano::node & node_a) :
	config{ config_a },
	node{ node_a },
	stats{ node_a.stats },
	logger{ node_a.logger },
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

void nano::rep_crawler::validate_and_process (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (lock.owns_lock ());
	debug_assert (!responses.empty ()); // Should be checked before calling this function

	decltype (responses) responses_l{ responses.capacity () };
	responses_l.swap (responses);

	lock.unlock ();

	// normally the rep_crawler only tracks principal reps but it can be made to track
	// reps with less weight by setting rep_crawler_weight_minimum to a low value
	auto const minimum = std::min (node.minimum_principal_weight (), node.config.rep_crawler_weight_minimum.number ());

	// TODO: Is it really faster to repeatedly lock/unlock the mutex for each response?
	for (auto const & response : responses_l)
	{
		auto & vote = response.second;
		auto & channel = response.first;
		release_assert (vote != nullptr);
		release_assert (channel != nullptr);

		if (channel->get_type () == nano::transport::transport_type::loopback)
		{
			logger.debug (nano::log::type::repcrawler, "Ignoring vote from loopback channel: {}", channel->to_string ());
			continue;
		}

		nano::uint128_t const rep_weight = node.ledger.weight (vote->account);
		if (rep_weight < minimum)
		{
			logger.debug (nano::log::type::repcrawler, "Ignoring vote from account {} with too little voting weight: {}",
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
			reps.modify (existing, [rep_weight, &updated, &vote, &channel, &prev_channel] (rep_entry & rep) {
				rep.last_response = std::chrono::steady_clock::now ();

				// Update if representative channel was changed
				if (rep.channel->get_endpoint () != channel->get_endpoint ())
				{
					debug_assert (rep.account == vote->account);
					updated = true;
					prev_channel = rep.channel;
					rep.channel = channel;
				}
			});
		}
		else
		{
			reps.emplace (rep_entry{ vote->account, channel });
			inserted = true;
		}

		lock.unlock ();

		if (inserted)
		{
			logger.info (nano::log::type::repcrawler, "Found representative {} at {}", vote->account.to_account (), channel->to_string ());
		}
		if (updated)
		{
			logger.warn (nano::log::type::repcrawler, "Updated representative {} at {} (was at: {})", vote->account.to_account (), channel->to_string (), prev_channel->to_string ());
		}
	}
}

std::chrono::milliseconds nano::rep_crawler::query_interval (bool sufficient_weight) const
{
	return sufficient_weight ? network_constants.rep_crawler_normal_interval : network_constants.rep_crawler_warmup_interval;
}

bool nano::rep_crawler::query_predicate (bool sufficient_weight) const
{
	return nano::elapsed (last_query, query_interval (sufficient_weight));
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

		condition.wait_for (lock, query_interval (sufficient_weight), [this, sufficient_weight] {
			return stopped || query_predicate (sufficient_weight) || !responses.empty ();
		});

		if (stopped)
		{
			return;
		}

		stats.inc (nano::stat::type::rep_crawler, nano::stat::detail::loop);

		if (!responses.empty ())
		{
			validate_and_process (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}

		cleanup ();

		if (query_predicate (sufficient_weight))
		{
			last_query = std::chrono::steady_clock::now ();

			lock.unlock ();

			auto targets = prepare_crawl_targets (sufficient_weight);
			query (targets);

			lock.lock ();
		}

		debug_assert (lock.owns_lock ());
	}
}

void nano::rep_crawler::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	// Evict reps with dead channels
	erase_if (reps, [this] (rep_entry const & rep) {
		if (!rep.channel->alive ())
		{
			logger.debug (nano::log::type::repcrawler, "Evicting representative {} with dead channel at {}", rep.account.to_account (), rep.channel->to_string ());
			stats.inc (nano::stat::type::rep_crawler, nano::stat::detail::channel_dead);
			return true; // Erase
		}
		return false;
	});

	// Evict reps that haven't responded in a while
	erase_if (reps, [this] (rep_entry const & rep) {
		if (nano::elapsed (rep.last_response, config.rep_timeout))
		{
			logger.debug (nano::log::type::repcrawler, "Evicting unresponsive representative {} at {}", rep.account.to_account (), rep.channel->to_string ());
			stats.inc (nano::stat::type::rep_crawler, nano::stat::detail::rep_timeout);
			return true; // Erase
		}
		return false;
	});

	// Evict queries that haven't been responded to in a while
	erase_if (queries, [this] (query_entry const & query) {
		if (nano::elapsed (query.time, config.query_timeout))
		{
			logger.debug (nano::log::type::repcrawler, "Evicting unresponsive query for block {} from {}", query.hash.to_string (), query.channel->to_string ());
			stats.inc (nano::stat::type::rep_crawler, nano::stat::detail::query_timeout);
			return true; // Erase
		}
		return false;
	});
}

std::vector<std::shared_ptr<nano::transport::channel>> nano::rep_crawler::prepare_crawl_targets (bool sufficient_weight) const
{
	// TODO: Make these values configurable
	constexpr std::size_t conservative_count = 10;
	constexpr std::size_t aggressive_count = 40;

	// Crawl more aggressively if we lack sufficient total peer weight.
	auto required_peer_count = sufficient_weight ? conservative_count : aggressive_count;

	stats.inc (nano::stat::type::rep_crawler, sufficient_weight ? nano::stat::detail::crawl_normal : nano::stat::detail::crawl_aggressive);

	// Add random peers. We do this even if we have enough weight, in order to pick up reps
	// that didn't respond when first observed. If the current total weight isn't sufficient, this
	// will be more aggressive. When the node first starts, the rep container is empty and all
	// endpoints will originate from random peers.
	required_peer_count += required_peer_count / 2;

	// The rest of the endpoints are picked randomly
	auto random_peers = node.network.random_set (required_peer_count, 0, true); // Include channels with ephemeral remote ports
	return { random_peers.begin (), random_peers.end () };
}

auto nano::rep_crawler::prepare_query_target () -> std::optional<hash_root_t>
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

		for (auto i = 0; queries.get<tag_hash> ().count (hash_root->first) != 0 && i < max_attempts; ++i)
		{
			hash_root = node.ledger.hash_root_random (transaction);
		}
	}

	return hash_root;
}

void nano::rep_crawler::track_rep_request (hash_root_t hash_root, std::shared_ptr<nano::transport::channel> const & channel)
{
	debug_assert (!mutex.try_lock ());

	debug_assert (queries.count (channel) == 0); // Only a single query should be active per channel
	queries.emplace (query_entry{ hash_root.first, channel });

	// Find and update the timestamp on all reps available on the endpoint (a single host may have multiple reps)
	auto & index = reps.get<tag_channel> ();
	auto [begin, end] = index.equal_range (channel);
	for (auto it = begin; it != end; ++it)
	{
		index.modify (it, [] (rep_entry & info) {
			info.last_request = std::chrono::steady_clock::now ();
		});
	}
}

void nano::rep_crawler::query (std::vector<std::shared_ptr<nano::transport::channel>> const & target_channels)
{
	auto maybe_hash_root = prepare_query_target ();
	if (!maybe_hash_root)
	{
		stats.inc (nano::stat::type::rep_crawler, nano::stat::detail::query_target_failed);
		return;
	}
	auto hash_root = *maybe_hash_root;

	nano::lock_guard<nano::mutex> lock{ mutex };

	for (const auto & channel : target_channels)
	{
		debug_assert (channel != nullptr);

		// Only a single query should be active per channel
		if (queries.find (channel) == queries.end ())
		{
			track_rep_request (hash_root, channel);
			node.network.send_confirm_req (channel, hash_root);

			logger.debug (nano::log::type::repcrawler, "Sending query for block {} to {}", hash_root.first.to_string (), channel->to_string ());
			stats.inc (nano::stat::type::rep_crawler, nano::stat::detail::query_sent);
		}
		else
		{
			stats.inc (nano::stat::type::rep_crawler, nano::stat::detail::query_channel_busy);
		}
	}
}

void nano::rep_crawler::query (std::shared_ptr<nano::transport::channel> const & target_channel)
{
	query (std::vector{ target_channel });
}

bool nano::rep_crawler::is_pr (std::shared_ptr<nano::transport::channel> const & channel) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto existing = reps.get<tag_channel> ().find (channel);
	if (existing != reps.get<tag_channel> ().end ())
	{
		return node.ledger.weight (existing->account) >= node.minimum_principal_weight ();
	}
	return false;
}

bool nano::rep_crawler::process (std::shared_ptr<nano::vote> const & vote, std::shared_ptr<nano::transport::channel> const & channel)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	if (auto info = queries.find (channel); info != queries.end ())
	{
		// TODO: This linear search could be slow, especially with large votes.
		auto const target_hash = info->hash;
		bool found = std::any_of (vote->hashes.begin (), vote->hashes.end (), [&target_hash] (nano::block_hash const & hash) {
			return hash == target_hash;
		});

		if (found)
		{
			logger.debug (nano::log::type::repcrawler, "Processing response for block {} from {}", target_hash.to_string (), channel->to_string ());
			stats.inc (nano::stat::type::rep_crawler, nano::stat::detail::response);
			// TODO: Track query response time

			responses.push_back ({ channel, vote });

			queries.erase (info);
			return true;
		}
	}
	return false;
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

std::vector<nano::representative> nano::rep_crawler::representatives (std::size_t count_a, nano::uint128_t const weight_a, boost::optional<decltype (nano::network_constants::protocol_version)> const & opt_version_min_a)
{
	auto const version_min = opt_version_min_a.value_or (node.network_params.network.protocol_version_min);

	nano::lock_guard<nano::mutex> lock{ mutex };

	std::multimap<nano::amount, rep_entry, std::greater<>> ordered;
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
	reps.emplace (rep_entry{ account, channel });
}

// Only for tests
void nano::rep_crawler::force_process (const std::shared_ptr<nano::vote> & vote, const std::shared_ptr<nano::transport::channel> & channel)
{
	release_assert (node.network_params.network.is_dev_network ());
	nano::lock_guard<nano::mutex> lock{ mutex };
	responses.push_back ({ channel, vote });
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

/*
 * rep_crawler_config
 */

nano::rep_crawler_config::rep_crawler_config (nano::network_constants const & network_constants)
{
	if (network_constants.is_dev_network ())
	{
		rep_timeout = std::chrono::milliseconds{ 1000 * 3 };
		query_timeout = std::chrono::milliseconds{ 1000 };
	}
}

nano::error nano::rep_crawler_config::serialize (nano::tomlconfig & toml) const
{
	// TODO: Descriptions
	toml.put ("rep_timeout", rep_timeout.count ());
	toml.put ("query_timeout", query_timeout.count ());

	return toml.get_error ();
}

nano::error nano::rep_crawler_config::deserialize (nano::tomlconfig & toml)
{
	auto rep_timeout_l = rep_timeout.count ();
	toml.get ("rep_timeout", rep_timeout_l);
	rep_timeout = std::chrono::milliseconds{ rep_timeout_l };

	auto query_timeout_l = query_timeout.count ();
	toml.get ("query_timeout", query_timeout_l);
	query_timeout = std::chrono::milliseconds{ query_timeout_l };

	return toml.get_error ();
}