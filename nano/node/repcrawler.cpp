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
	nano::lock_guard<nano::mutex> lock (active_mutex);
	active.erase (hash_a);
}

void nano::rep_crawler::start ()
{
	ongoing_crawl ();
}

void nano::rep_crawler::validate ()
{
	decltype (responses) responses_l;
	{
		nano::lock_guard<nano::mutex> lock (active_mutex);
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
			if (node.config.logging.rep_crawler_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("rep_crawler ignoring vote from loopback channel %1%") % channel->to_string ()));
			}
			continue;
		}

		nano::uint128_t rep_weight = node.ledger.weight (vote->account);
		if (rep_weight < minimum)
		{
			if (node.config.logging.rep_crawler_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("rep_crawler ignoring vote from account %1% with too little voting weight %2%") % vote->account.to_account () % rep_weight));
			}
			continue;
		}

		// temporary data used for logging after dropping the lock
		auto inserted = false;
		auto updated = false;
		std::shared_ptr<nano::transport::channel> prev_channel;

		nano::unique_lock<nano::mutex> lock (probable_reps_mutex);

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
					info.weight = rep_weight;
					prev_channel = info.channel;
					info.channel = channel;
				}
			});
		}
		else
		{
			probable_reps.emplace (nano::representative (vote->account, rep_weight, channel));
			inserted = true;
		}

		lock.unlock ();

		if (inserted)
		{
			node.logger.try_log (boost::str (boost::format ("Found representative %1% at %2%") % vote->account.to_account () % channel->to_string ()));
		}

		if (updated)
		{
			node.logger.try_log (boost::str (boost::format ("Updated representative %1% at %2% (was at: %3%)") % vote->account.to_account () % channel->to_string () % prev_channel->to_string ()));
		}
	}
}

void nano::rep_crawler::ongoing_crawl ()
{
	auto now (std::chrono::steady_clock::now ());
	auto total_weight_l (total_weight ());
	cleanup_reps ();
	update_weights ();
	validate ();
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

std::vector<std::shared_ptr<nano::transport::channel>> nano::rep_crawler::get_crawl_targets (nano::uint128_t total_weight_a)
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
	auto random_peers (node.network.random_set (required_peer_count, 0)); // Include channels with ephemeral remote ports
	std::vector<std::shared_ptr<nano::transport::channel>> result;
	result.insert (result.end (), random_peers.begin (), random_peers.end ());
	return result;
}

void nano::rep_crawler::query (std::vector<std::shared_ptr<nano::transport::channel>> const & channels_a)
{
	auto transaction (node.store.tx_begin_read ());
	auto hash_root (node.ledger.hash_root_random (transaction));
	{
		nano::lock_guard<nano::mutex> lock (active_mutex);
		// Don't send same block multiple times in tests
		if (node.network_params.network.is_dev_network ())
		{
			for (auto i (0); active.count (hash_root.first) != 0 && i < 4; ++i)
			{
				hash_root = node.ledger.hash_root_random (transaction);
			}
		}
		active.insert (hash_root.first);
	}
	if (!channels_a.empty ())
	{
		// In case our random block is a recently confirmed one, we remove an entry otherwise votes will be marked as replay and not forwarded to repcrawler
		node.active.recently_confirmed.erase (hash_root.first);
	}
	for (auto i (channels_a.begin ()), n (channels_a.end ()); i != n; ++i)
	{
		debug_assert (*i != nullptr);
		on_rep_request (*i);
		node.network.send_confirm_req (*i, hash_root);
	}

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

void nano::rep_crawler::query (std::shared_ptr<nano::transport::channel> const & channel_a)
{
	std::vector<std::shared_ptr<nano::transport::channel>> peers;
	peers.emplace_back (channel_a);
	query (peers);
}

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
	nano::lock_guard<nano::mutex> lock (probable_reps_mutex);
	auto existing = probable_reps.get<tag_channel_ref> ().find (channel_a);
	bool result = false;
	if (existing != probable_reps.get<tag_channel_ref> ().end ())
	{
		result = existing->weight > node.minimum_principal_weight ();
	}
	return result;
}

bool nano::rep_crawler::response (std::shared_ptr<nano::transport::channel> const & channel_a, std::shared_ptr<nano::vote> const & vote_a, bool force)
{
	bool error = true;
	nano::lock_guard<nano::mutex> lock (active_mutex);
	for (auto i = vote_a->hashes.begin (), n = vote_a->hashes.end (); i != n; ++i)
	{
		if (force || active.count (*i) != 0)
		{
			responses.emplace_back (channel_a, vote_a);
			error = false;
			break;
		}
	}
	return error;
}

nano::uint128_t nano::rep_crawler::total_weight () const
{
	nano::lock_guard<nano::mutex> lock (probable_reps_mutex);
	nano::uint128_t result (0);
	for (auto i (probable_reps.get<tag_weight> ().begin ()), n (probable_reps.get<tag_weight> ().end ()); i != n; ++i)
	{
		auto weight (i->weight.number ());
		if (weight > 0)
		{
			result = result + weight;
		}
		else
		{
			break;
		}
	}
	return result;
}

void nano::rep_crawler::on_rep_request (std::shared_ptr<nano::transport::channel> const & channel_a)
{
	nano::lock_guard<nano::mutex> lock (probable_reps_mutex);
	if (channel_a->get_tcp_endpoint ().address () != boost::asio::ip::address_v6::any ())
	{
		probably_rep_t::index<tag_channel_ref>::type & channel_ref_index = probable_reps.get<tag_channel_ref> ();

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
		nano::lock_guard<nano::mutex> lock (probable_reps_mutex);
		auto iterator (probable_reps.get<tag_last_request> ().begin ());
		while (iterator != probable_reps.get<tag_last_request> ().end ())
		{
			if (iterator->channel->get_tcp_endpoint ().address () != boost::asio::ip::address_v6::any ())
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
			if (find_channel != nullptr && *find_channel == *static_cast<nano::transport::channel_tcp *> (i.get ()))
			{
				equal = true;
			}
		}
		else if (i->get_type () == nano::transport::transport_type::udp)
		{
			auto find_channel (node.network.udp_channels.channel (i->get_endpoint ()));
			if (find_channel != nullptr && *find_channel == *static_cast<nano::transport::channel_udp *> (i.get ()))
			{
				equal = true;
			}
		}
		if (!equal)
		{
			nano::lock_guard<nano::mutex> lock (probable_reps_mutex);
			probable_reps.get<tag_channel_ref> ().erase (*i);
		}
	}
}

void nano::rep_crawler::update_weights ()
{
	nano::lock_guard<nano::mutex> lock (probable_reps_mutex);
	for (auto i (probable_reps.get<tag_last_request> ().begin ()), n (probable_reps.get<tag_last_request> ().end ()); i != n;)
	{
		auto weight (node.ledger.weight (i->account));
		if (weight > 0)
		{
			if (i->weight.number () != weight)
			{
				probable_reps.get<tag_last_request> ().modify (i, [weight] (nano::representative & info) {
					info.weight = weight;
				});
			}
			++i;
		}
		else
		{
			// Erase non representatives
			i = probable_reps.get<tag_last_request> ().erase (i);
		}
	}
}

std::vector<nano::representative> nano::rep_crawler::representatives (std::size_t count_a, nano::uint128_t const weight_a, boost::optional<decltype (nano::network_constants::protocol_version)> const & opt_version_min_a)
{
	auto version_min (opt_version_min_a.value_or (node.network_params.network.protocol_version_min));
	std::vector<representative> result;
	nano::lock_guard<nano::mutex> lock (probable_reps_mutex);
	for (auto i (probable_reps.get<tag_weight> ().begin ()), n (probable_reps.get<tag_weight> ().end ()); i != n && result.size () < count_a; ++i)
	{
		if (i->weight > weight_a && i->channel->get_network_version () >= version_min)
		{
			result.push_back (*i);
		}
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
	auto reps (representatives (count_a));
	for (auto const & rep : reps)
	{
		result.push_back (rep.channel);
	}
	return result;
}

/** Total number of representatives */
std::size_t nano::rep_crawler::representative_count ()
{
	nano::lock_guard<nano::mutex> lock (probable_reps_mutex);
	return probable_reps.size ();
}
