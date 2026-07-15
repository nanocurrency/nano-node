#pragma once

#include <nano/lib/node_capabilities.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/traffic_type.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <deque>
#include <memory>
#include <span>

namespace mi = boost::multi_index;

namespace nano::bootstrap
{
enum class peer_acquire_status
{
	acquired, // A matching peer was reserved
	busy, // Matching peers exist but are all at capacity; the caller should wait for capacity
	exhausted, // Every matching peer was rejected by the exclusion predicate; the caller should conclude the work
	no_peers, // No connected peer satisfies the capability requirement; the caller should wait for peers
};

enum class peer_probe_status
{
	available, // acquire () would reserve a peer right now, ignoring the channel send queue residual
	busy, // Matching peers exist but are all at capacity
	none, // No capable non-excluded peer exists
};

nano::stat::detail to_stat_detail (peer_acquire_status);
nano::stat::detail to_stat_detail (peer_probe_status);

/*
 * Pool of peers usable for bootstrap requests, tracked together with their in-flight request load.
 * A peer is acquired for each outgoing request, reserving one slot of its capacity, and released when the
 * request concludes. The periodic update () tracks newly connected channels and drops closed ones; the
 * periodic decay () shrinks loads that drift upwards when responses are lost.
 *
 * Peer identity (node id) and capabilities are cached per entry so selection scans avoid locking each channel.
 * Channels are held by shared_ptr and liveness is checked only in update (), so a closed channel may be
 * offered for up to one update interval;
 *
 * Not internally synchronized: callers must hold the bootstrap mutex.
 */
class peer_pool
{
public:
	static nano::transport::traffic_type constexpr default_traffic_type = nano::transport::traffic_type::bootstrap;

	explicit peer_pool (nano::bootstrap_config const &);

	struct acquire_result
	{
		std::shared_ptr<nano::transport::channel> channel;
		peer_acquire_status status{ peer_acquire_status::no_peers };
		nano::account node_id{ 0 }; // Cached identity of the acquired peer
	};

	// Reserves the least-loaded peer that satisfies the capability requirement and is not excluded.
	// The exclusion list lets a fanout round route each of its requests to a distinct peer.
	acquire_result acquire (nano::node_capabilities_flags required = {}, std::span<nano::account const> exclude = {}, nano::transport::traffic_type traffic = default_traffic_type);

	// Returns one reserved capacity slot when a response arrives
	void release (std::shared_ptr<nano::transport::channel> const &);

	// Returns true if any peer satisfies the capability requirement and is not excluded, ignoring capacity
	bool has_candidate (nano::node_capabilities_flags required = {}, std::span<nano::account const> exclude = {}) const;

	// Capacity-aware, non-reserving mirror of acquire (). This intentionally does not inspect the
	// channel send queue; acquire () performs that final check on the selected candidate.
	peer_probe_status probe (nano::node_capabilities_flags required = {}, std::span<nano::account const> exclude = {}) const;

	// Tracks newly connected channels and drops closed ones
	void update (std::deque<std::shared_ptr<nano::transport::channel>> const & channels);

	// Decays the load of unanswered requests
	void decay ();

	void reset ();

	std::size_t size () const;
	std::size_t available () const;

	nano::container_info container_info () const;

private: // Dependencies
	nano::bootstrap_config const & config;

private:
	struct entry
	{
		entry (std::shared_ptr<nano::transport::channel>, nano::account, nano::node_capabilities_flags);

		std::shared_ptr<nano::transport::channel> channel;

		// Cached so selection scans do not need to lock the channel
		nano::account node_id;
		nano::node_capabilities_flags capabilities;

		// Number of requests sent to the peer and not yet concluded
		uint64_t outstanding{ 0 };

		bool capable (nano::node_capabilities_flags required) const
		{
			return (capabilities & required) == required;
		}

		void decay ()
		{
			outstanding -= outstanding > 0 ? 1 : 0;
		}
	};

	// clang-format off
	class tag_channel {};
	class tag_outstanding {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_channel>,
			mi::member<entry, std::shared_ptr<nano::transport::channel>, &entry::channel>>,
		mi::ordered_non_unique<mi::tag<tag_outstanding>,
			mi::member<entry, uint64_t, &entry::outstanding>>
	>>;
	// clang-format on

	ordered_entries entries;
};
}
