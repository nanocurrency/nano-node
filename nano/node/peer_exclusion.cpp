#include <nano/node/peer_exclusion.hpp>

nano::peer_exclusion::peer_exclusion (std::size_t max_size_a) :
	max_size{ max_size_a }
{
}

uint64_t nano::peer_exclusion::add (nano::tcp_endpoint const & endpoint)
{
	uint64_t result = 0;
	nano::lock_guard<nano::mutex> guard{ mutex };

	if (auto existing = peers.get<tag_endpoint> ().find (endpoint.address ()); existing == peers.get<tag_endpoint> ().end ())
	{
		// Clean old excluded peers
		while (peers.size () > 1 && peers.size () >= max_size)
		{
			peers.get<tag_exclusion> ().erase (peers.get<tag_exclusion> ().begin ());
		}
		debug_assert (peers.size () <= max_size);

		// Insert new endpoint
		auto inserted = peers.insert (peer_exclusion::item{ std::chrono::steady_clock::steady_clock::now () + exclude_time_hours, endpoint.address (), 1 });
		(void)inserted;
		debug_assert (inserted.second);
		result = 1;
	}
	else
	{
		// Update existing endpoint
		peers.get<tag_endpoint> ().modify (existing, [&result] (peer_exclusion::item & item_a) {
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

uint64_t nano::peer_exclusion::score (const nano::tcp_endpoint & endpoint) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	if (auto existing = peers.get<tag_endpoint> ().find (endpoint.address ()); existing != peers.get<tag_endpoint> ().end ())
	{
		return existing->score;
	}
	return 0;
}

std::chrono::steady_clock::time_point nano::peer_exclusion::until (const nano::tcp_endpoint & endpoint) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	if (auto existing = peers.get<tag_endpoint> ().find (endpoint.address ()); existing != peers.get<tag_endpoint> ().end ())
	{
		return existing->exclude_until;
	}
	return {};
}

bool nano::peer_exclusion::check (nano::tcp_endpoint const & endpoint) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	if (auto existing = peers.get<tag_endpoint> ().find (endpoint.address ()); existing != peers.get<tag_endpoint> ().end ())
	{
		if (existing->score >= score_limit && existing->exclude_until > std::chrono::steady_clock::now ())
		{
			return true;
		}
	}
	return false;
}

void nano::peer_exclusion::remove (nano::tcp_endpoint const & endpoint_a)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	peers.get<tag_endpoint> ().erase (endpoint_a.address ());
}

std::size_t nano::peer_exclusion::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return peers.size ();
}

std::unique_ptr<nano::container_info_component> nano::peer_exclusion::collect_container_info (std::string const & name)
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "peers", peers.size (), sizeof (decltype (peers)::value_type) }));
	return composite;
}
