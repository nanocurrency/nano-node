#include <nano/node/node.hpp>
#include <nano/node/repcrawler.hpp>

nano::rep_crawler::rep_crawler (nano::node & node_a) :
node (node_a)
{
	node.observers.endpoint.add ([this](nano::endpoint const & endpoint_a) {
		this->query (endpoint_a);
	});
}

void nano::rep_crawler::add (nano::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (active_mutex);
	active.insert (hash_a);
}

void nano::rep_crawler::remove (nano::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (active_mutex);
	active.erase (hash_a);
}

bool nano::rep_crawler::exists (nano::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (active_mutex);
	return active.count (hash_a) != 0;
}

void nano::rep_crawler::start ()
{
	ongoing_crawl ();
}

void nano::rep_crawler::ongoing_crawl ()
{
	auto now (std::chrono::steady_clock::now ());
	query (get_crawl_targets ());
	// Reduce crawl frequency when there's enough total peer weight
	unsigned next_run_seconds = (total_weight_internal () > node.config.online_weight_minimum.number ()) ? 7 : 3;
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.alarm.add (now + std::chrono::seconds (next_run_seconds), [node_w, this]() {
		if (auto node_l = node_w.lock ())
		{
			this->ongoing_crawl ();
		}
	});
}

std::vector<nano::endpoint> nano::rep_crawler::get_crawl_targets ()
{
	std::unordered_set<nano::endpoint> endpoints;
	constexpr size_t conservative_count = 10;
	constexpr size_t aggressive_count = 40;

	// Crawl more aggressively if we lack sufficient total peer weight.
	bool sufficient_weight (total_weight_internal () > node.config.online_weight_minimum.number ());
	uint16_t required_peer_count = sufficient_weight ? conservative_count : aggressive_count;
	std::lock_guard<std::mutex> lock (probable_reps_mutex);

	// First, add known rep endpoints, ordered by ascending last-requested time.
	for (auto i (probable_reps.get<tag_last_request> ().begin ()), n (probable_reps.get<tag_last_request> ().end ()); i != n && endpoints.size () < required_peer_count; ++i)
	{
		endpoints.insert (i->endpoint);
	};

	// Add additional random peers. We do this even if we have enough weight, in order to pick up reps
	// that didn't respond when first observed. If the current total weight isn't sufficient, this
	// will be more aggressive. When the node first starts, the rep container is empty and all
	// endpoints will originate from random peers.
	required_peer_count += required_peer_count / 2;

	// The rest of the endpoints are picked randomly
	auto random_peers (node.peers.list ());
	for (auto & peer : random_peers)
	{
		endpoints.insert (peer);
		if (endpoints.size () >= required_peer_count)
		{
			break;
		}
	}

	std::vector<nano::endpoint> result;
	result.insert (result.end (), endpoints.begin (), endpoints.end ());
	return result;
}

void nano::rep_crawler::query (std::vector<nano::endpoint> const & endpoints_a)
{
	auto transaction (node.store.tx_begin_read ());
	std::shared_ptr<nano::block> block (node.store.block_random (transaction));
	auto hash (block->hash ());
	add (hash);
	for (auto i (endpoints_a.begin ()), n (endpoints_a.end ()); i != n; ++i)
	{
		on_rep_request (*i);
		nano::confirm_req message (block);
		node.network.send_buffer (message.to_bytes (), *i, [](boost::system::error_code const &, size_t) {});
	}

	// A representative must respond with a vote within the deadline
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w, hash]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->rep_crawler.remove (hash);
		}
	});
}

void nano::rep_crawler::query (nano::endpoint const & endpoint_a)
{
	std::vector<nano::endpoint> peers;
	peers.push_back (endpoint_a);
	query (peers);
}

bool nano::rep_crawler::response (nano::endpoint const & endpoint_a, nano::account const & rep_account_a, nano::amount const & weight_a)
{
	assert (endpoint_a.address ().is_v6 ());
	auto updated (false);
	std::lock_guard<std::mutex> lock (probable_reps_mutex);
	auto existing (probable_reps.find (rep_account_a));
	if (existing != probable_reps.end ())
	{
		probable_reps.modify (existing, [weight_a, &updated, rep_account_a, endpoint_a](nano::representative & info) {
			info.last_response = std::chrono::steady_clock::now ();

			if (info.weight < weight_a)
			{
				updated = true;
				info.weight = weight_a;
				info.endpoint = endpoint_a;
				info.account = rep_account_a;
			}
		});
	}
	else
	{
		probable_reps.insert (nano::representative (rep_account_a, weight_a, endpoint_a));
	}
	return updated;
}

nano::uint128_t nano::rep_crawler::total_weight_internal ()
{
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

nano::uint128_t nano::rep_crawler::total_weight ()
{
	std::lock_guard<std::mutex> lock (probable_reps_mutex);
	return total_weight_internal ();
}

std::vector<nano::representative> nano::rep_crawler::representatives_by_weight ()
{
	std::vector<nano::representative> result;
	std::lock_guard<std::mutex> lock (probable_reps_mutex);
	for (auto i (probable_reps.get<tag_weight> ().begin ()), n (probable_reps.get<tag_weight> ().end ()); i != n; ++i)
	{
		auto weight (i->weight.number ());
		if (weight > 0)
		{
			result.push_back (*i);
		}
		else
		{
			break;
		}
	}
	return result;
}

void nano::rep_crawler::on_rep_request (nano::endpoint const & endpoint_a)
{
	std::lock_guard<std::mutex> lock (probable_reps_mutex);

	using probable_rep_itr_t = probably_rep_t::index<tag_endpoint>::type::iterator;
	probably_rep_t::index<tag_endpoint>::type & endpoint_index = probable_reps.get<tag_endpoint> ();

	// Find and update the timestamp on all reps available on the endpoint (a single host may have multiple reps)
	std::vector<probable_rep_itr_t> view;
	auto itr_pair = probable_reps.get<tag_endpoint> ().equal_range (endpoint_a);
	for (; itr_pair.first != itr_pair.second; itr_pair.first++)
	{
		endpoint_index.modify (itr_pair.first, [](nano::representative & value_a) {
			value_a.last_request = std::chrono::steady_clock::now ();
		});
	}
}

std::vector<nano::representative> nano::rep_crawler::representatives (size_t count_a)
{
	std::vector<representative> result;
	result.reserve (std::min (count_a, size_t (16)));
	std::lock_guard<std::mutex> lock (probable_reps_mutex);
	for (auto i (probable_reps.get<tag_weight> ().begin ()), n (probable_reps.get<tag_weight> ().end ()); i != n && result.size () < count_a; ++i)
	{
		if (!i->weight.is_zero ())
		{
			result.push_back (*i);
		}
	}
	return result;
}

std::vector<nano::endpoint> nano::rep_crawler::representative_endpoints (size_t count_a)
{
	std::vector<nano::endpoint> result;
	auto reps (representatives (count_a));
	for (auto rep : reps)
	{
		result.push_back (rep.endpoint);
	}
	return result;
}

/** Total number of representatives */
size_t nano::rep_crawler::representative_count ()
{
	std::lock_guard<std::mutex> lock (probable_reps_mutex);
	return probable_reps.size ();
}
