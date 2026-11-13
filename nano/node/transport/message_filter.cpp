#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/stream.hpp>
#include <nano/messages/message_header.hpp>
#include <nano/messages/message_type.hpp>
#include <nano/node/network.hpp>
#include <nano/node/transport/message_filter.hpp>

#include <vector>

namespace
{
// Domain of a payload in the filter: the message type and, for a publish, the block type, which together decide how the bytes are read
uint64_t domain_of (nano::messages::message_type message_type, nano::block_type block_type)
{
	// Other messages have a block type in their header too, but it has no say in how their payload is read
	auto const subtype = message_type == nano::messages::message_type::publish ? block_type : nano::block_type::invalid;
	return (static_cast<uint64_t> (message_type) << 8) | static_cast<uint64_t> (subtype);
}
}

nano::transport::message_filter::message_filter (nano::network_config const & config_a) :
	config{ config_a },
	filter{ config.duplicate_filter_size, config.duplicate_filter_cutoff }
{
}

bool nano::transport::message_filter::deduplicates (nano::messages::message_type type) const
{
	if (!config.duplicate_filter_enable)
	{
		return false;
	}

	switch (type)
	{
		// Blocks and votes are flooded, so the same one arrives from many peers
		case nano::messages::message_type::publish:
		case nano::messages::message_type::confirm_ack:
			return true;
		// The rest are requests, replies to them and periodic updates, where a repeat is a message in its own right
		case nano::messages::message_type::invalid:
		case nano::messages::message_type::not_a_type:
		case nano::messages::message_type::keepalive:
		case nano::messages::message_type::confirm_req:
		case nano::messages::message_type::bulk_pull:
		case nano::messages::message_type::bulk_push:
		case nano::messages::message_type::frontier_req:
		case nano::messages::message_type::node_id_handshake:
		case nano::messages::message_type::bulk_pull_account:
		case nano::messages::message_type::telemetry_req:
		case nano::messages::message_type::telemetry_ack:
		case nano::messages::message_type::asc_pull_req:
		case nano::messages::message_type::asc_pull_ack:
		case nano::messages::message_type::vote_relay_req:
		case nano::messages::message_type::vote_relay_ack:
			return false;
	}

	// No default case, so the compiler flags a message type missing above; values outside the enum end up here
	return false;
}

nano::filter_digest nano::transport::message_filter::hash (nano::messages::message_header const & header, std::span<uint8_t const> payload) const
{
	// Hashing reads only the filter's key, which never changes, so it runs without the lock
	auto const domain = domain_of (header.type, header.block_type ());
	return filter.hash (payload, domain);
}

nano::filter_digest nano::transport::message_filter::hash (nano::block const & block) const
{
	// A publish payload is the serialized block
	std::vector<uint8_t> payload;
	{
		nano::vectorstream stream{ payload };
		block.serialize (stream);
	}

	auto const domain = domain_of (nano::messages::message_type::publish, block.type ());
	return filter.hash (payload, domain);
}

bool nano::transport::message_filter::insert (nano::filter_digest const & digest)
{
	std::lock_guard guard{ mutex };
	return filter.insert (digest);
}

bool nano::transport::message_filter::contains (nano::filter_digest const & digest) const
{
	std::lock_guard guard{ mutex };
	return filter.contains (digest);
}

void nano::transport::message_filter::erase (nano::filter_digest const & digest)
{
	std::lock_guard guard{ mutex };
	filter.erase (digest);
}

void nano::transport::message_filter::erase (nano::block const & block)
{
	erase (hash (block));
}

void nano::transport::message_filter::clear ()
{
	std::lock_guard guard{ mutex };
	filter.clear ();
}

void nano::transport::message_filter::advance (uint64_t epochs)
{
	std::lock_guard guard{ mutex };
	filter.advance (epochs);
}
