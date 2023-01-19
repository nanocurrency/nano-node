#include <nano/node/peer_exclusion.hpp>

constexpr std::chrono::hours nano::peer_exclusion::exclude_time_hours;
constexpr std::chrono::hours nano::peer_exclusion::exclude_remove_hours;
constexpr std::size_t nano::peer_exclusion::size_max;
constexpr double nano::peer_exclusion::peers_percentage_limit;

uint64_t nano::peer_exclusion::add (nano::tcp_endpoint const & endpoint_a, std::size_t const network_peers_count_a)
{
	uint64_t result (0);
	nano::lock_guard<nano::mutex> guard (mutex);
	// Clean old excluded peers
	auto limited = limited_size (network_peers_count_a);
	while (peers.size () > 1 && peers.size () > limited)
	{
		peers.get<tag_exclusion> ().erase (peers.get<tag_exclusion> ().begin ());
	}
	debug_assert (peers.size () <= size_max);
	auto & peers_by_endpoint (peers.get<tag_endpoint> ());
	auto address = endpoint_a.address ();
	auto existing (peers_by_endpoint.find (address));
	if (existing == peers_by_endpoint.end ())
	{
		// Insert new endpoint
		auto inserted (peers.insert (peer_exclusion::item{ std::chrono::steady_clock::steady_clock::now () + exclude_time_hours, address, 1 }));
		(void)inserted;
		debug_assert (inserted.second);
		result = 1;
	}
	else
	{
		// Update existing endpoint
		peers_by_endpoint.modify (existing, [&result] (peer_exclusion::item & item_a) {
			++item_a.score;
			result = item_a.score;
			if (item_a.score == peer_exclusion::score_limit)
			{
				item_a.exclude_until = std::chrono::steady_clock::now () + peer_exclusion::exclude_time_hours;
			}
			else if (item_a.score > peer_exclusion::score_limit)
			{
				item_a.exclude_until = std::chrono::steady_clock::now () + peer_exclusion::exclude_time_hours * item_a.score * 2;
			}
		});
	}
	return result;
}

bool nano::peer_exclusion::check (nano::tcp_endpoint const & endpoint_a)
{
	bool excluded (false);
	nano::lock_guard<nano::mutex> guard (mutex);
	auto & peers_by_endpoint (peers.get<tag_endpoint> ());
	auto existing (peers_by_endpoint.find (endpoint_a.address ()));
	if (existing != peers_by_endpoint.end () && existing->score >= score_limit)
	{
		if (existing->exclude_until > std::chrono::steady_clock::now ())
		{
			excluded = true;
		}
		else if (existing->exclude_until + exclude_remove_hours * existing->score < std::chrono::steady_clock::now ())
		{
			peers_by_endpoint.erase (existing);
		}
	}
	return excluded;
}

void nano::peer_exclusion::remove (nano::tcp_endpoint const & endpoint_a)
{
	nano::lock_guard<nano::mutex> guard (mutex);
	peers.get<tag_endpoint> ().erase (endpoint_a.address ());
}

std::size_t nano::peer_exclusion::limited_size (std::size_t const network_peers_count_a) const
{
	return std::min (size_max, static_cast<std::size_t> (network_peers_count_a * peers_percentage_limit));
}

std::size_t nano::peer_exclusion::size () const
{
	nano::lock_guard<nano::mutex> guard (mutex);
	return peers.size ();
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (nano::peer_exclusion const & excluded_peers, std::string const & name)
{
	auto composite = std::make_unique<container_info_composite> (name);

	std::size_t excluded_peers_count = excluded_peers.size ();
	auto sizeof_excluded_peers_element = sizeof (nano::peer_exclusion::ordered_endpoints::value_type);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "peers", excluded_peers_count, sizeof_excluded_peers_element }));

	return composite;
}
