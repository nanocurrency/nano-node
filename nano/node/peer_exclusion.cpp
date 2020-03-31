#include <nano/node/peer_exclusion.hpp>

constexpr std::chrono::hours nano::peer_exclusion::exclude_time_hours;
constexpr std::chrono::hours nano::peer_exclusion::exclude_remove_hours;
constexpr size_t nano::peer_exclusion::size_max;

uint64_t nano::peer_exclusion::add (nano::tcp_endpoint const & endpoint_a, size_t const network_peers_count_a)
{
	uint64_t result (0);
	nano::lock_guard<std::mutex> guard (mutex);
	// Clean old excluded peers
	while (peers.size () > 1 && peers.size () > std::min<size_t> (size_max, network_peers_count_a * peers_percentage_limit))
	{
		peers.get<tag_exclusion> ().erase (peers.get<tag_exclusion> ().begin ());
	}
	debug_assert (peers.size () <= size_max);
	auto & peers_by_endpoint (peers.get<tag_endpoint> ());
	auto existing (peers_by_endpoint.find (endpoint_a));
	if (existing == peers_by_endpoint.end ())
	{
		// Insert new endpoint
		auto inserted (peers.emplace (peer_exclusion::item{ std::chrono::steady_clock::steady_clock::now () + exclude_time_hours, endpoint_a, 1 }));
		(void)inserted;
		debug_assert (inserted.second);
		result = 1;
	}
	else
	{
		// Update existing endpoint
		peers_by_endpoint.modify (existing, [&result](peer_exclusion::item & item_a) {
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
	nano::lock_guard<std::mutex> guard (mutex);
	auto & peers_by_endpoint (peers.get<tag_endpoint> ());
	auto existing (peers_by_endpoint.find (endpoint_a));
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
	nano::lock_guard<std::mutex> guard (mutex);
	peers.get<tag_endpoint> ().erase (endpoint_a);
}

size_t nano::peer_exclusion::size () const
{
	nano::lock_guard<std::mutex> guard (mutex);
	return peers.size ();
}
