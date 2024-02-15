#include <nano/node/node.hpp>
#include <nano/node/repcrawler.hpp>

#include <boost/format.hpp>

nano::rep_crawler::rep_crawler (nano::node & node_a) :
	node (node_a)
{
	if (!node.flags.disable_rep_crawler)
	{
		node.observers.endpoint.add ([this] (std::shared_ptr<nano::transport::channel> const & channel_a) {
			this->query (channel_a);
		});
	}
}

void nano::rep_crawler::remove (nano::block_hash const & hash_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	active.erase (hash_a);
}

void nano::rep_crawler::start ()
{
	ongoing_crawl ();
}

void nano::rep_crawler::validate_and_process ()
{
	decltype (responses) responses_l;
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		responses_l.swap (responses);
	}

	// normally the rep_crawler only tracks principal reps but it can be made to track
	// reps with less weight by setting rep_crawler_weight_minimum to a low value
	auto minimum = std::min (node.minimum_principal_weight (), node.config.rep_crawler_weight_minimum.number ());

	for (auto const & i : responses_l)
	{
		auto & vote = i.second;
		auto & channel = i.first;
		debug_assert (channel != nullptr);

		if (channel->get_type () == nano::transport::transport_type::loopback)
		{
			node.logger.debug (nano::log::type::repcrawler, "Ignoring vote from loopback channel: {}", channel->to_string ());

			continue;
		}

		nano::uint128_t rep_weight = node.ledger.weight (vote->account);
		if (rep_weight < minimum)
		{
			node.logger.debug (nano::log::type::repcrawler, "Ignoring vote from account {} with too little voting weight: {}",
			vote->account.to_account (),
			nano::util::to_str (rep_weight));

			continue;
		}

		// temporary data used for logging after dropping the lock
		auto inserted = false;
		auto updated = false;
		std::shared_ptr<nano::transport::channel> prev_channel;

		nano::unique_lock<nano::mutex> lock{ mutex };

		auto existing (probable_reps.find (vote->account));
		if (existing != probable_reps.end ())
		{
			probable_reps.modify (existing, [rep_weight, &updated, &vote, &channel, &prev_channel] (nano::representative & info) {
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
			probable_reps.emplace (nano::representative (vote->account, channel));
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

void nano::rep_crawler::ongoing_crawl ()
{
	auto now (std::chrono::steady_clock::now ());
	auto total_weight_l (total_weight ());
	cleanup_reps ();
	validate_and_process ();
	query (get_crawl_targets (total_weight_l));
	auto sufficient_weight (total_weight_l > node.online_reps.delta ());
	// If online weight drops below minimum, reach out to preconfigured peers
	if (!sufficient_weight)
	{
		node.keepalive_preconfigured (node.config.preconfigured_peers);
	}
	// Reduce crawl frequency when there's enough total peer weight
	unsigned next_run_ms = node.network_params.network.is_dev_network () ? 100 : sufficient_weight ? 7000
																								   : 3000;
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.workers.add_timed_task (now + std::chrono::milliseconds (next_run_ms), [node_w, this] () {
		if (auto node_l = node_w.lock ())
		{
			this->ongoing_crawl ();
		}
	});
}

std::vector<std::shared_ptr<nano::transport::channel>> nano::rep_crawler::get_crawl_targets (nano::uint128_t total_weight_a) const
{
	constexpr std::size_t conservative_count = 10;
	constexpr std::size_t aggressive_count = 40;

	// Crawl more aggressively if we lack sufficient total peer weight.
	bool sufficient_weight (total_weight_a > node.online_reps.delta ());
	uint16_t required_peer_count = sufficient_weight ? conservative_count : aggressive_count;

	// Add random peers. We do this even if we have enough weight, in order to pick up reps
	// that didn't respond when first observed. If the current total weight isn't sufficient, this
	// will be more aggressive. When the node first starts, the rep container is empty and all
	// endpoints will originate from random peers.
	required_peer_count += required_peer_count / 2;

	// The rest of the endpoints are picked randomly
	auto random_peers (node.network.random_set (required_peer_count, 0, true)); // Include channels with ephemeral remote ports
	std::vector<std::shared_ptr<nano::transport::channel>> result;
	result.insert (result.end (), random_peers.begin (), random_peers.end ());
	return result;
}

void nano::rep_crawler::query (std::vector<std::shared_ptr<nano::transport::channel>> const & channels_a)
{
	auto transaction (node.store.tx_begin_read ());
	std::optional<std::pair<nano::block_hash, nano::block_hash>> hash_root;
	for (auto i = 0; i < 4 && !hash_root; ++i)
	{
		hash_root = node.ledger.hash_root_random (transaction);
		if (node.active.recently_confirmed.exists (hash_root->first))
		{
			hash_root = std::nullopt;
		}
	}
	if (!hash_root)
	{
		return;
	}
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		// Don't send same block multiple times in tests
		if (node.network_params.network.is_dev_network ())
		{
			for (auto i (0); active.count (hash_root->first) != 0 && i < 4; ++i)
			{
				hash_root = node.ledger.hash_root_random (transaction);
			}
		}
		active.insert (hash_root->first); // TODO: Is this really necessary?
	}
	for (const auto & channel : channels_a)
	{
		debug_assert (channel != nullptr);
		on_rep_request (channel);
		node.network.send_confirm_req (channel, *hash_root);
	}

	// TODO: Use a thread+timeout instead of a timer
	// A representative must respond with a vote within the deadline
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w, hash = hash_root->first] () {
		if (auto node_l = node_w.lock ())
		{
			auto target_finished_processed (node_l->vote_processor.total_processed + node_l->vote_processor.size ());
			node_l->rep_crawler.throttled_remove (hash, target_finished_processed);
		}
	});
}

void nano::rep_crawler::query (std::shared_ptr<nano::transport::channel> const & channel_a)
{
	query (std::vector{ channel_a });
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
	auto existing = probable_reps.get<tag_channel_ref> ().find (channel_a);
	if (existing != probable_reps.get<tag_channel_ref> ().end ())
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
		if (force || active.count (hash) != 0)
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
	nano::uint128_t result (0);
	for (const auto & i : probable_reps.get<tag_account> ())
	{
		if (i.channel->alive ())
		{
			result += node.ledger.weight (i.account);
		}
	}
	return result;
}

void nano::rep_crawler::on_rep_request (std::shared_ptr<nano::transport::channel> const & channel_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	if (channel_a->get_tcp_endpoint ().address () != boost::asio::ip::address_v6::any ())
	{
		auto & channel_ref_index = probable_reps.get<tag_channel_ref> ();

		// Find and update the timestamp on all reps available on the endpoint (a single host may have multiple reps)
		auto itr_pair = channel_ref_index.equal_range (*channel_a);
		for (; itr_pair.first != itr_pair.second; itr_pair.first++)
		{
			channel_ref_index.modify (itr_pair.first, [] (nano::representative & value_a) {
				value_a.last_request = std::chrono::steady_clock::now ();
			});
		}
	}
}

void nano::rep_crawler::cleanup_reps ()
{
	std::vector<std::shared_ptr<nano::transport::channel>> channels;
	{
		// Check known rep channels
		nano::lock_guard<nano::mutex> lock{ mutex };
		auto iterator (probable_reps.get<tag_last_request> ().begin ());
		while (iterator != probable_reps.get<tag_last_request> ().end ())
		{
			if (iterator->channel->alive ())
			{
				channels.push_back (iterator->channel);
				++iterator;
			}
			else
			{
				// Remove reps with closed channels
				iterator = probable_reps.get<tag_last_request> ().erase (iterator);
			}
		}
	}
	// Remove reps with inactive channels
	for (auto const & i : channels)
	{
		bool equal (false);
		if (i->get_type () == nano::transport::transport_type::tcp)
		{
			auto find_channel (node.network.tcp_channels.find_channel (i->get_tcp_endpoint ()));
			if (find_channel != nullptr && *find_channel == *static_cast<nano::transport::channel_tcp *> (i.get ())) // TODO: WTF
			{
				equal = true;
			}
		}
		else if (i->get_type () == nano::transport::transport_type::fake)
		{
			equal = true;
		}
		if (!equal)
		{
			nano::lock_guard<nano::mutex> lock{ mutex };
			probable_reps.get<tag_channel_ref> ().erase (*i);
		}
	}
}

std::vector<nano::representative> nano::rep_crawler::representatives (std::size_t count_a, nano::uint128_t const weight_a, boost::optional<decltype (nano::network_constants::protocol_version)> const & opt_version_min_a)
{
	auto version_min = opt_version_min_a.value_or (node.network_params.network.protocol_version_min);
	std::multimap<nano::amount, representative, std::greater<nano::amount>> ordered;

	nano::lock_guard<nano::mutex> lock{ mutex };

	for (const auto & i : probable_reps.get<tag_account> ())
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
		result.push_back (i->second);
	}
	return result;
}

std::vector<nano::representative> nano::rep_crawler::principal_representatives (std::size_t count_a, boost::optional<decltype (nano::network_constants::protocol_version)> const & opt_version_min_a)
{
	return representatives (count_a, node.minimum_principal_weight (), opt_version_min_a);
}

std::vector<std::shared_ptr<nano::transport::channel>> nano::rep_crawler::representative_endpoints (std::size_t count_a)
{
	std::vector<std::shared_ptr<nano::transport::channel>> result;
	auto reps = representatives (count_a);
	for (auto const & rep : reps)
	{
		result.push_back (rep.channel);
	}
	return result;
}

/** Total number of representatives */
std::size_t nano::rep_crawler::representative_count ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return probable_reps.size ();
}

// Only for tests
void nano::rep_crawler::force_add_rep (const nano::account & account, const std::shared_ptr<nano::transport::channel> & channel)
{
	release_assert (node.network_params.network.is_dev_network ());
	nano::lock_guard<nano::mutex> lock{ mutex };
	probable_reps.emplace (nano::representative (account, channel));
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
void nano::rep_crawler::force_active (const nano::block_hash & hash)
{
	release_assert (node.network_params.network.is_dev_network ());
	nano::lock_guard<nano::mutex> lock{ mutex };
	active.insert (hash);
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (rep_crawler & rep_crawler, std::string const & name)
{
	std::size_t count;
	{
		nano::lock_guard<nano::mutex> guard{ rep_crawler.mutex };
		count = rep_crawler.active.size ();
	}

	auto const sizeof_element = sizeof (decltype (rep_crawler.active)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "active", count, sizeof_element }));
	return composite;
}