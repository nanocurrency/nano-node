#pragma once

#include <nano/node/fwd.hpp>
#include <nano/node/transport/traffic_type.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <deque>
#include <memory>

namespace mi = boost::multi_index;

namespace nano::bootstrap
{
// Container for tracking and scoring peers with respect to bootstrapping
class peer_scoring
{
public:
	static nano::transport::traffic_type constexpr traffic_type = nano::transport::traffic_type::bootstrap_requests;

public:
	peer_scoring (bootstrap_config const &, nano::network_constants const &);

	// Returns true if channel limit has been exceeded
	bool limit_exceeded (std::shared_ptr<nano::transport::channel> const & channel) const;
	bool try_send_message (std::shared_ptr<nano::transport::channel> const & channel);
	void received_message (std::shared_ptr<nano::transport::channel> const & channel);

	std::shared_ptr<nano::transport::channel> channel ();

	// Synchronize channels with the network, passed channels should be shuffled
	void sync (std::deque<std::shared_ptr<nano::transport::channel>> const & list);

	// Cleans up scores for closed channels
	// Decays scores which become inaccurate over time due to message drops
	void timeout ();

	std::size_t size () const;
	std::size_t available () const;

	nano::container_info container_info () const;

private:
	bootstrap_config const & config;
	nano::network_constants const & network_constants;

private:
	class peer_score
	{
	public:
		explicit peer_score (std::shared_ptr<nano::transport::channel> const &, uint64_t, uint64_t, uint64_t);
		std::weak_ptr<nano::transport::channel> channel;
		// std::weak_ptr does not provide ordering so the naked pointer is also tracked and used for ordering channels
		// This pointer may be invalid if the channel has been destroyed
		nano::transport::channel * channel_ptr;
		// Acquire reference to the shared channel object if it is still valid
		[[nodiscard]] std::shared_ptr<nano::transport::channel> shared () const
		{
			auto result = channel.lock ();
			if (result)
			{
				debug_assert (result.get () == channel_ptr);
			}
			return result;
		}
		void decay ()
		{
			outstanding = outstanding > 0 ? outstanding - 1 : 0;
		}
		// Number of outstanding requests to a peer
		uint64_t outstanding{ 0 };
		uint64_t request_count_total{ 0 };
		uint64_t response_count_total{ 0 };
	};

	// clang-format off
	// Indexes scores by their shared channel pointer
	class tag_channel {};
	// Indexes scores by the number of outstanding requests in ascending order
	class tag_outstanding {};

	using ordered_scoring = boost::multi_index_container<peer_score,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_channel>,
			mi::member<peer_score, nano::transport::channel *, &peer_score::channel_ptr>>,
		mi::ordered_non_unique<mi::tag<tag_outstanding>,
			mi::member<peer_score, uint64_t, &peer_score::outstanding>>>>;
	// clang-format on
	ordered_scoring scoring;

	std::deque<std::shared_ptr<nano::transport::channel>> channels;
};
}
