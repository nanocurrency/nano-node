#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap/peer_scoring.hpp>
#include <nano/node/transport/channel.hpp>

/*
 * peer_scoring
 */

nano::bootstrap::peer_scoring::peer_scoring (bootstrap_config const & config_a, nano::network_constants const & network_constants_a) :
	config{ config_a },
	network_constants{ network_constants_a }
{
}

void nano::bootstrap::peer_scoring::reset ()
{
	scoring.clear ();
}

bool nano::bootstrap::peer_scoring::limit_exceeded (std::shared_ptr<nano::transport::channel> const & channel) const
{
	auto & index = scoring.get<tag_channel> ();
	if (auto existing = index.find (channel.get ()); existing != index.end ())
	{
		return existing->outstanding >= config.channel_limit;
	}
	return false;
}

bool nano::bootstrap::peer_scoring::try_insert (std::shared_ptr<nano::transport::channel> const & channel)
{
	auto & index = scoring.get<tag_channel> ();
	if (index.find (channel.get ()) == index.end ())
	{
		index.emplace (channel, 0, 0, 0);
		return true;
	}
	return false;
}

bool nano::bootstrap::peer_scoring::try_send_message (std::shared_ptr<nano::transport::channel> const & channel)
{
	auto & index = scoring.get<tag_channel> ();
	auto existing = index.find (channel.get ());
	if (existing == index.end ())
	{
		index.emplace (channel, 1, 1, 0);
	}
	else
	{
		if (existing->outstanding < config.channel_limit)
		{
			[[maybe_unused]] auto success = index.modify (existing, [] (auto & score) {
				++score.outstanding;
				++score.request_count_total;
			});
			debug_assert (success);
		}
		else
		{
			return false; // Channel limit exceeded
		}
	}
	return true;
}

void nano::bootstrap::peer_scoring::received_message (std::shared_ptr<nano::transport::channel> const & channel)
{
	auto & index = scoring.get<tag_channel> ();
	if (auto existing = index.find (channel.get ()); existing != index.end ())
	{
		if (existing->outstanding > 1)
		{
			[[maybe_unused]] auto success = index.modify (existing, [] (auto & score) {
				--score.outstanding;
				++score.response_count_total;
			});
			debug_assert (success);
		}
	}
}

std::shared_ptr<nano::transport::channel> nano::bootstrap::peer_scoring::channel ()
{
	// Iterate channels in ascending order of outstanding requests
	auto & index = scoring.get<tag_outstanding> ();
	for (auto const & entry : index)
	{
		auto channel = entry.channel.lock ();
		if (channel && !channel->max (traffic_type))
		{
			if (try_send_message (channel))
			{
				return channel;
			}
		}
	}
	return nullptr;
}

std::size_t nano::bootstrap::peer_scoring::size () const
{
	return scoring.size ();
}

std::size_t nano::bootstrap::peer_scoring::available () const
{
	return std::count_if (scoring.begin (), scoring.end (), [this] (auto const & entry) {
		auto channel = entry.channel.lock ();
		return channel && !limit_exceeded (channel);
	});
}

void nano::bootstrap::peer_scoring::timeout ()
{
	erase_if (scoring, [] (auto const & score) {
		if (auto channel = score.shared ())
		{
			if (channel->alive ())
			{
				return false; // Keep
			}
		}
		return true;
	});

	for (auto score = scoring.begin (), n = scoring.end (); score != n; ++score)
	{
		scoring.modify (score, [] (auto & score_a) {
			score_a.decay ();
		});
	}
}

void nano::bootstrap::peer_scoring::sync (std::deque<std::shared_ptr<nano::transport::channel>> const & list)
{
	for (auto const & channel : list)
	{
		try_insert (channel);
	}
}

nano::container_info nano::bootstrap::peer_scoring::container_info () const
{
	nano::container_info info;
	info.put ("scoring", size ());
	info.put ("available", available ());
	return info;
}

/*
 * peer_score
 */

nano::bootstrap::peer_scoring::peer_score::peer_score (std::shared_ptr<nano::transport::channel> const & channel_a, uint64_t outstanding_a, uint64_t request_count_total_a, uint64_t response_count_total_a) :
	channel{ channel_a },
	channel_ptr{ channel_a.get () },
	outstanding{ outstanding_a },
	request_count_total{ request_count_total_a },
	response_count_total{ response_count_total_a }
{
}
