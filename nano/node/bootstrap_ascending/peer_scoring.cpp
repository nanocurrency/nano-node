#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap_ascending/peer_scoring.hpp>
#include <nano/node/transport/channel.hpp>

/*
 * peer_scoring
 */

nano::bootstrap_ascending::peer_scoring::peer_scoring (bootstrap_ascending_config const & config_a, nano::network_constants const & network_constants_a) :
	config{ config_a },
	network_constants{ network_constants_a }
{
}

bool nano::bootstrap_ascending::peer_scoring::try_send_message (std::shared_ptr<nano::transport::channel> channel)
{
	auto & index = scoring.get<tag_channel> ();
	auto existing = index.find (channel.get ());
	if (existing == index.end ())
	{
		index.emplace (channel, 1, 1, 0);
	}
	else
	{
		if (existing->outstanding < config.requests_limit)
		{
			[[maybe_unused]] auto success = index.modify (existing, [] (auto & score) {
				++score.outstanding;
				++score.request_count_total;
			});
			debug_assert (success);
		}
		else
		{
			return true;
		}
	}
	return false;
}

void nano::bootstrap_ascending::peer_scoring::received_message (std::shared_ptr<nano::transport::channel> channel)
{
	auto & index = scoring.get<tag_channel> ();
	auto existing = index.find (channel.get ());
	if (existing != index.end ())
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

std::shared_ptr<nano::transport::channel> nano::bootstrap_ascending::peer_scoring::channel ()
{
	auto & index = scoring.get<tag_outstanding> ();
	for (auto const & score : index)
	{
		if (auto channel = score.shared ())
		{
			if (!channel->max ())
			{
				if (!try_send_message (channel))
				{
					return channel;
				}
			}
		}
	}
	return nullptr;
}

std::size_t nano::bootstrap_ascending::peer_scoring::size () const
{
	return scoring.size ();
}

void nano::bootstrap_ascending::peer_scoring::timeout ()
{
	auto & index = scoring.get<tag_channel> ();

	erase_if (index, [] (auto const & score) {
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

void nano::bootstrap_ascending::peer_scoring::sync (std::deque<std::shared_ptr<nano::transport::channel>> const & list)
{
	auto & index = scoring.get<tag_channel> ();
	for (auto const & channel : list)
	{
		if (channel->get_network_version () >= network_constants.bootstrap_protocol_version_min)
		{
			if (index.find (channel.get ()) == index.end ())
			{
				if (!channel->max (nano::transport::traffic_type::bootstrap))
				{
					index.emplace (channel, 1, 1, 0);
				}
			}
		}
	}
}

/*
 * peer_score
 */

nano::bootstrap_ascending::peer_scoring::peer_score::peer_score (
std::shared_ptr<nano::transport::channel> const & channel_a, uint64_t outstanding_a, uint64_t request_count_total_a, uint64_t response_count_total_a) :
	channel{ channel_a },
	channel_ptr{ channel_a.get () },
	outstanding{ outstanding_a },
	request_count_total{ request_count_total_a },
	response_count_total{ response_count_total_a }
{
}
