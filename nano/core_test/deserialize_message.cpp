#include <nano/lib/block_type.hpp>
#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/network_filter.hpp>
#include <nano/lib/node_capabilities.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/vote.hpp>
#include <nano/lib/work.hpp>
#include <nano/messages/deserialize_message.hpp>
#include <nano/messages/messages.hpp>
#include <nano/messages/uniquers.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/test_common/random.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <typeinfo>
#include <utility>
#include <variant>
#include <vector>

namespace
{
std::vector<uint8_t> serialize_header (nano::messages::message_header const & header)
{
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		header.serialize (stream);
	}
	return bytes;
}

// The part of a serialized message that follows its header
nano::buffer_view payload_of (std::vector<uint8_t> const & bytes)
{
	return nano::buffer_view{ bytes }.subspan (nano::messages::message_header::size);
}

// Name of the status a message type reports for a payload it cannot parse
std::string invalid_status (nano::messages::message_type type)
{
	return "invalid_" + std::string{ nano::messages::to_string (type) } + "_message";
}

// State block with enough work to pass the work check
std::shared_ptr<nano::block> make_block (nano::work_pool & pool)
{
	nano::keypair key;
	nano::block_builder builder;
	auto const previous = nano::test::random_hash ();
	return builder
	.state ()
	.account (key.pub)
	.previous (previous)
	.representative (key.pub)
	.balance (1)
	.link (nano::test::random_hash ())
	.sign (key.prv, key.pub)
	.work (*pool.generate (previous))
	.build ();
}

std::vector<nano::block_hash> random_hashes (std::size_t count)
{
	std::vector<nano::block_hash> hashes;
	for (std::size_t n = 0; n < count; ++n)
	{
		hashes.push_back (nano::test::random_hash ());
	}
	return hashes;
}

std::vector<std::pair<nano::block_hash, nano::root>> random_roots_hashes (std::size_t count)
{
	std::vector<std::pair<nano::block_hash, nano::root>> roots_hashes;
	for (std::size_t n = 0; n < count; ++n)
	{
		roots_hashes.emplace_back (nano::test::random_hash (), nano::test::random_hash ());
	}
	return roots_hashes;
}

struct sample
{
	std::string name;
	std::unique_ptr<nano::messages::message> message;
};

// A message of every type, plus each variant that changes how the header declares the payload length
std::vector<sample> make_samples ()
{
	auto & network = nano::dev::network_params.network;
	nano::work_pool pool{ network, std::numeric_limits<unsigned>::max () };
	nano::keypair key;
	nano::block_builder builder;
	auto const previous = nano::test::random_hash ();

	std::vector<sample> samples;
	auto add = [&samples] (std::string name, std::unique_ptr<nano::messages::message> message) {
		samples.push_back ({ std::move (name), std::move (message) });
	};

	add ("keepalive", std::make_unique<nano::messages::keepalive> (network));

	// Publish takes its payload length from the block type
	add ("publish send", std::make_unique<nano::messages::publish> (network, builder.send ().previous (previous).destination (key.pub).balance (1).sign (key.prv, key.pub).work (*pool.generate (previous)).build ()));
	add ("publish receive", std::make_unique<nano::messages::publish> (network, builder.receive ().previous (previous).source (nano::test::random_hash ()).sign (key.prv, key.pub).work (*pool.generate (previous)).build ()));
	add ("publish open", std::make_unique<nano::messages::publish> (network, builder.open ().source (nano::test::random_hash ()).representative (key.pub).account (key.pub).sign (key.prv, key.pub).work (*pool.generate (key.pub)).build ()));
	add ("publish change", std::make_unique<nano::messages::publish> (network, builder.change ().previous (previous).representative (key.pub).sign (key.prv, key.pub).work (*pool.generate (previous)).build ()));
	add ("publish state", std::make_unique<nano::messages::publish> (network, make_block (pool)));

	// Confirmations move their entry count to a wider header field from 16 entries on, 255 is the most it holds
	for (std::size_t count : { 1, 16, 255 })
	{
		add ("confirm_req " + std::to_string (count), std::make_unique<nano::messages::confirm_req> (network, random_roots_hashes (count)));
		add ("confirm_ack " + std::to_string (count), std::make_unique<nano::messages::confirm_ack> (network, nano::test::make_vote (key, random_hashes (count))));
	}
	// A vote may carry no hashes at all, a request must name at least one
	add ("confirm_ack 0", std::make_unique<nano::messages::confirm_ack> (network, nano::test::make_vote (key, random_hashes (0))));

	add ("bulk_pull", std::make_unique<nano::messages::bulk_pull> (network));
	{
		// A header flag announces the optional count
		auto message = std::make_unique<nano::messages::bulk_pull> (network);
		message->count = 7;
		message->set_count_present (true);
		add ("bulk_pull count", std::move (message));
	}
	{
		auto message = std::make_unique<nano::messages::bulk_pull_account> (network);
		message->account = key.pub;
		message->minimum_amount = 1;
		message->flags = nano::messages::bulk_pull_account_flags::pending_address_only;
		add ("bulk_pull_account", std::move (message));
	}
	add ("bulk_push", std::make_unique<nano::messages::bulk_push> (network));
	{
		auto message = std::make_unique<nano::messages::frontier_req> (network);
		message->start = key.pub;
		message->age = 1;
		message->count = 2;
		add ("frontier_req", std::move (message));
	}

	// A handshake payload depends on the query and response flags and on the response version
	nano::messages::node_id_handshake::query_payload query;
	query.cookie = nano::test::random_hash ();
	auto make_response = [&key, &query] (auto const & ext) {
		nano::messages::node_id_handshake::response_payload response;
		response.node_id = key.pub;
		response.ext = ext;
		response.sign (query.cookie, key);
		return response;
	};
	nano::messages::node_id_handshake::response_payload::v2_payload const v2{ nano::test::random_hash (), nano::test::random_hash () };
	nano::messages::node_id_handshake::response_payload::v3_payload const v3{ nano::test::random_hash (), nano::test::random_hash (), nano::node_capabilities::topo_index };
	add ("node_id_handshake query", std::make_unique<nano::messages::node_id_handshake> (network, query));
	add ("node_id_handshake response v1", std::make_unique<nano::messages::node_id_handshake> (network, std::nullopt, make_response (std::monostate{})));
	add ("node_id_handshake response v2", std::make_unique<nano::messages::node_id_handshake> (network, std::nullopt, make_response (v2)));
	add ("node_id_handshake response v3", std::make_unique<nano::messages::node_id_handshake> (network, std::nullopt, make_response (v3)));
	add ("node_id_handshake query response", std::make_unique<nano::messages::node_id_handshake> (network, query, make_response (v3)));

	add ("telemetry_req", std::make_unique<nano::messages::telemetry_req> (network));

	// Telemetry declares its payload length in the header, which leaves room for fields added by newer versions
	add ("telemetry_ack empty", std::make_unique<nano::messages::telemetry_ack> (network));
	{
		nano::messages::telemetry_data data;
		data.node_id = key.pub;
		data.version = nano::messages::telemetry_data_version::v1;
		add ("telemetry_ack v1", std::make_unique<nano::messages::telemetry_ack> (network, data));
		data.version = nano::messages::telemetry_data_version::v2;
		add ("telemetry_ack v2", std::make_unique<nano::messages::telemetry_ack> (network, data));
		data.unknown_data = { 0xaa, 0xbb, 0xcc };
		add ("telemetry_ack unknown data", std::make_unique<nano::messages::telemetry_ack> (network, data));
	}

	// Ascending bootstrap messages declare their payload length in the header
	auto add_asc_pull_req = [&add, &network] (std::string name, nano::messages::asc_pull_type type, nano::messages::asc_pull_req::payload_variant payload) {
		auto message = std::make_unique<nano::messages::asc_pull_req> (network);
		message->id = 7;
		message->type = type;
		message->payload = std::move (payload);
		message->update_header ();
		add (std::move (name), std::move (message));
	};
	add_asc_pull_req ("asc_pull_req blocks", nano::messages::asc_pull_type::blocks, nano::messages::asc_pull_req::blocks_payload{ .start = nano::test::random_hash_or_account (), .count = 128, .start_type = nano::messages::asc_pull_req::hash_type::block });
	add_asc_pull_req ("asc_pull_req account_info", nano::messages::asc_pull_type::account_info, nano::messages::asc_pull_req::account_info_payload{ .target = nano::test::random_hash_or_account (), .target_type = nano::messages::asc_pull_req::hash_type::account });
	add_asc_pull_req ("asc_pull_req frontiers", nano::messages::asc_pull_type::frontiers, nano::messages::asc_pull_req::frontiers_payload{ .start = nano::test::random_account (), .count = 1000 });
	add_asc_pull_req ("asc_pull_req blocks_random", nano::messages::asc_pull_type::blocks_random, nano::messages::asc_pull_req::blocks_random_payload{ .hashes = { nano::test::random_hash (), nano::test::random_hash () } });
	add_asc_pull_req ("asc_pull_req topo_index", nano::messages::asc_pull_type::topo_index, nano::messages::asc_pull_req::topo_index_payload{ .start = nano::topo_key{ 42, nano::test::random_hash () }, .count = 777 });

	auto add_asc_pull_ack = [&add, &network] (std::string name, nano::messages::asc_pull_type type, nano::messages::asc_pull_ack::payload_variant payload) {
		auto message = std::make_unique<nano::messages::asc_pull_ack> (network);
		message->id = 11;
		message->type = type;
		message->payload = std::move (payload);
		message->update_header ();
		add (std::move (name), std::move (message));
	};
	add_asc_pull_ack ("asc_pull_ack blocks", nano::messages::asc_pull_type::blocks, nano::messages::asc_pull_ack::blocks_payload{ .blocks = { nano::test::random_block (), nano::test::random_block () } });
	add_asc_pull_ack ("asc_pull_ack blocks empty", nano::messages::asc_pull_type::blocks, nano::messages::asc_pull_ack::blocks_payload{});
	add_asc_pull_ack ("asc_pull_ack account_info", nano::messages::asc_pull_type::account_info, nano::messages::asc_pull_ack::account_info_payload{ .account = nano::test::random_account (), .account_open = nano::test::random_hash (), .account_head = nano::test::random_hash (), .account_block_count = 932932132, .account_conf_frontier = nano::test::random_hash (), .account_conf_height = 847312 });
	add_asc_pull_ack ("asc_pull_ack frontiers", nano::messages::asc_pull_type::frontiers, nano::messages::asc_pull_ack::frontiers_payload{ .frontiers = { { nano::test::random_account (), nano::test::random_hash () } } });
	add_asc_pull_ack ("asc_pull_ack topo_index", nano::messages::asc_pull_type::topo_index, nano::messages::asc_pull_ack::topo_index_payload{ .entries = { nano::topo_key{ 1, nano::test::random_hash () }, nano::topo_key{ 2, nano::test::random_hash () } } });
	add_asc_pull_ack ("asc_pull_ack topo_index empty", nano::messages::asc_pull_type::topo_index, nano::messages::asc_pull_ack::topo_index_payload{});

	return samples;
}
}

/*
 * Every message type the protocol defines passes header validation and parses back to the bytes it was serialized from,
 * while the two placeholder values are rejected
 */
TEST (deserialize_message_header, message_types)
{
	auto const & network = nano::dev::network_params.network;
	for (auto type : nano::enum_values<nano::messages::message_type> ())
	{
		SCOPED_TRACE (nano::messages::to_string (type));
		nano::messages::message_header header{ network, type };
		header.block_type_set (nano::block_type::state); // Publish derives its payload length from the block type
		auto const bytes = serialize_header (header);

		auto [parsed, status] = nano::deserialize_message_header (bytes, network);
		if (type == nano::messages::message_type::invalid || type == nano::messages::message_type::not_a_type)
		{
			ASSERT_FALSE (parsed);
			ASSERT_EQ (status, nano::deserialize_message_status::invalid_message_type);
		}
		else
		{
			ASSERT_TRUE (parsed);
			ASSERT_EQ (status, nano::deserialize_message_status::success);
			ASSERT_EQ (serialize_header (*parsed), bytes);
		}
	}
}

/*
 * Type values outside the enum, such as a retired type or one from a newer protocol, are rejected
 */
TEST (deserialize_message_header, unknown_message_type)
{
	auto const & network = nano::dev::network_params.network;
	auto const last_type = nano::enum_values<nano::messages::message_type> ().back ();
	std::vector<uint8_t> const unknown_types{ 0x09, static_cast<uint8_t> (static_cast<uint8_t> (last_type) + 1), 0xff };
	for (auto value : unknown_types)
	{
		SCOPED_TRACE (static_cast<int> (value));
		nano::messages::message_header header{ network, static_cast<nano::messages::message_type> (value) };

		auto [parsed, status] = nano::deserialize_message_header (serialize_header (header), network);
		ASSERT_FALSE (parsed);
		ASSERT_EQ (status, nano::deserialize_message_status::invalid_message_type);
	}
}

/*
 * A buffer shorter than a header is rejected at every length
 */
TEST (deserialize_message_header, short_buffer)
{
	auto const & network = nano::dev::network_params.network;
	auto const bytes = serialize_header (nano::messages::message_header{ network, nano::messages::message_type::keepalive });
	ASSERT_EQ (bytes.size (), nano::messages::message_header::size);
	for (std::size_t size = 0; size < bytes.size (); ++size)
	{
		SCOPED_TRACE (size);
		auto [parsed, status] = nano::deserialize_message_header (nano::buffer_view{ bytes.data (), size }, network);
		ASSERT_FALSE (parsed);
		ASSERT_EQ (status, nano::deserialize_message_status::invalid_header);
	}
}

/*
 * A header from another network is rejected
 */
TEST (deserialize_message_header, invalid_network)
{
	auto const & network = nano::dev::network_params.network;
	nano::messages::message_header header{ network, nano::messages::message_type::keepalive };
	header.network = nano::network_type::nano_live_network;
	ASSERT_NE (header.network, network.current_network);

	auto [parsed, status] = nano::deserialize_message_header (serialize_header (header), network);
	ASSERT_FALSE (parsed);
	ASSERT_EQ (status, nano::deserialize_message_status::invalid_network);
}

/*
 * The protocol version in use must be at least the minimum this node supports, newer versions are accepted
 */
TEST (deserialize_message_header, protocol_version)
{
	auto const & network = nano::dev::network_params.network;
	auto parse_version = [&network] (uint8_t version_using) {
		nano::messages::message_header header{ network, nano::messages::message_type::keepalive };
		header.version_using = version_using;
		return nano::deserialize_message_header (serialize_header (header), network);
	};

	auto [outdated, outdated_status] = parse_version (network.protocol_version_min - 1);
	ASSERT_FALSE (outdated);
	ASSERT_EQ (outdated_status, nano::deserialize_message_status::outdated_version);

	auto [oldest, oldest_status] = parse_version (network.protocol_version_min);
	ASSERT_TRUE (oldest);
	ASSERT_EQ (oldest_status, nano::deserialize_message_status::success);

	auto [newer, newer_status] = parse_version (network.protocol_version + 1);
	ASSERT_TRUE (newer);
	ASSERT_EQ (newer_status, nano::deserialize_message_status::success);
}

/*
 * A publish header must name an actual block type, since the payload length follows from it
 */
TEST (deserialize_message_header, publish_block_type)
{
	auto const & network = nano::dev::network_params.network;
	auto parse_block_type = [&network] (nano::block_type block_type) {
		nano::messages::message_header header{ network, nano::messages::message_type::publish };
		header.block_type_set (block_type);
		return nano::deserialize_message_header (serialize_header (header), network);
	};
	for (auto block_type : nano::enum_values<nano::block_type> ())
	{
		SCOPED_TRACE (nano::to_string (block_type));
		auto [parsed, status] = parse_block_type (block_type);
		if (block_type == nano::block_type::invalid || block_type == nano::block_type::not_a_block)
		{
			ASSERT_FALSE (parsed);
			ASSERT_EQ (status, nano::deserialize_message_status::invalid_header);
		}
		else
		{
			ASSERT_TRUE (parsed);
			ASSERT_EQ (status, nano::deserialize_message_status::success);
		}
	}

	// The header field has room for values the enum does not define
	auto [unknown, unknown_status] = parse_block_type (static_cast<nano::block_type> (0x0f));
	ASSERT_FALSE (unknown);
	ASSERT_EQ (unknown_status, nano::deserialize_message_status::invalid_header);
}

/*
 * A payload cut short of what its header declares is rejected with the status of its own message type
 */
TEST (deserialize_message_payload, truncated)
{
	auto const & network = nano::dev::network_params.network;
	for (auto const & sample : make_samples ())
	{
		auto const bytes = sample.message->to_bytes ();
		auto const payload = payload_of (*bytes);
		if (payload.empty ())
		{
			continue;
		}
		SCOPED_TRACE (sample.name);

		auto [message, status] = nano::deserialize_message_payload (payload.first (payload.size () - 1), sample.message->header, network);

		// A block list in an ascending bootstrap response ends wherever the payload stops parsing as a block, so losing its terminator loses no block
		auto const * asc_pull_ack = dynamic_cast<nano::messages::asc_pull_ack const *> (sample.message.get ());
		if (asc_pull_ack != nullptr && std::holds_alternative<nano::messages::asc_pull_ack::blocks_payload> (asc_pull_ack->payload))
		{
			ASSERT_EQ (status, nano::deserialize_message_status::success);
			ASSERT_NE (message, nullptr);
			auto const & blocks = std::get<nano::messages::asc_pull_ack::blocks_payload> (dynamic_cast<nano::messages::asc_pull_ack const &> (*message).payload).blocks;
			ASSERT_EQ (blocks.size (), std::get<nano::messages::asc_pull_ack::blocks_payload> (asc_pull_ack->payload).blocks.size ());
			continue;
		}
		ASSERT_EQ (message, nullptr);
		ASSERT_EQ (nano::to_string (status), invalid_status (sample.message->type ()));
	}
}

/*
 * A payload with bytes past the end of its message is rejected, except by the types that leave room for fields added by newer protocol versions
 */
TEST (deserialize_message_payload, trailing_byte)
{
	auto const & network = nano::dev::network_params.network;
	std::set<nano::messages::message_type> const extensible{ nano::messages::message_type::telemetry_ack, nano::messages::message_type::asc_pull_req, nano::messages::message_type::asc_pull_ack };
	for (auto const & sample : make_samples ())
	{
		auto bytes = *sample.message->to_bytes ();
		if (bytes.size () == nano::messages::message_header::size)
		{
			continue; // A message without a payload never reads one
		}
		SCOPED_TRACE (sample.name);
		bytes.push_back (0);

		auto [message, status] = nano::deserialize_message_payload (payload_of (bytes), sample.message->header, network);
		if (extensible.contains (sample.message->type ()))
		{
			ASSERT_NE (message, nullptr);
			ASSERT_EQ (status, nano::deserialize_message_status::success);
		}
		else
		{
			ASSERT_EQ (message, nullptr);
			ASSERT_EQ (nano::to_string (status), invalid_status (sample.message->type ()));
		}
	}
}

/*
 * A header that names no message type is rejected even when the caller skipped header validation
 */
TEST (deserialize_message_payload, invalid_message_type)
{
	auto const & network = nano::dev::network_params.network;
	std::vector<nano::messages::message_type> const types{ nano::messages::message_type::invalid, nano::messages::message_type::not_a_type, static_cast<nano::messages::message_type> (0xff) };
	for (auto type : types)
	{
		SCOPED_TRACE (static_cast<int> (type));
		nano::messages::message_header const header{ network, type };

		auto [message, status] = nano::deserialize_message_payload (nano::buffer_view{}, header, network);
		ASSERT_EQ (message, nullptr);
		ASSERT_EQ (status, nano::deserialize_message_status::invalid_message_type);
	}
}

/*
 * With a network filter, a repeated publish or vote is reported as a duplicate before it is parsed, while other message types are never filtered.
 * Without a filter nothing counts as a duplicate.
 */
TEST (deserialize_message_payload, duplicates)
{
	auto const & network = nano::dev::network_params.network;
	nano::network_filter filter{ 256 };
	for (auto const & sample : make_samples ())
	{
		SCOPED_TRACE (sample.name);
		auto const bytes = sample.message->to_bytes ();
		auto const payload = payload_of (*bytes);

		auto [first, first_status] = nano::deserialize_message_payload (payload, sample.message->header, network, &filter);
		ASSERT_NE (first, nullptr);
		ASSERT_EQ (first_status, nano::deserialize_message_status::success);

		auto [repeat, repeat_status] = nano::deserialize_message_payload (payload, sample.message->header, network, &filter);
		switch (sample.message->type ())
		{
			case nano::messages::message_type::publish:
			{
				ASSERT_EQ (repeat, nullptr);
				ASSERT_EQ (repeat_status, nano::deserialize_message_status::duplicate_publish_message);
			}
			break;
			case nano::messages::message_type::confirm_ack:
			{
				ASSERT_EQ (repeat, nullptr);
				ASSERT_EQ (repeat_status, nano::deserialize_message_status::duplicate_confirm_ack_message);
			}
			break;
			default:
			{
				ASSERT_NE (repeat, nullptr);
				ASSERT_EQ (repeat_status, nano::deserialize_message_status::success);
			}
			break;
		}

		auto [unfiltered, unfiltered_status] = nano::deserialize_message_payload (payload, sample.message->header, network);
		ASSERT_NE (unfiltered, nullptr);
		ASSERT_EQ (unfiltered_status, nano::deserialize_message_status::success);
	}
}

/*
 * A publish or vote that passed the network filter carries its digest, which lets the receiver clear it from the filter so the same message can arrive again
 */
TEST (deserialize_message_payload, digest)
{
	auto & network = nano::dev::network_params.network;
	nano::work_pool pool{ network, std::numeric_limits<unsigned>::max () };
	nano::network_filter filter{ 256 };

	nano::messages::publish const publish{ network, make_block (pool) };
	auto const publish_bytes = publish.to_bytes ();
	auto [publish_first, publish_first_status] = nano::deserialize_message_payload (payload_of (*publish_bytes), publish.header, network, &filter);
	ASSERT_NE (publish_first, nullptr);
	filter.clear (dynamic_cast<nano::messages::publish &> (*publish_first).digest);
	auto [publish_again, publish_again_status] = nano::deserialize_message_payload (payload_of (*publish_bytes), publish.header, network, &filter);
	ASSERT_NE (publish_again, nullptr);
	ASSERT_EQ (publish_again_status, nano::deserialize_message_status::success);

	nano::messages::confirm_ack const confirm_ack{ network, nano::test::make_vote (nano::keypair{}, random_hashes (1)) };
	auto const confirm_ack_bytes = confirm_ack.to_bytes ();
	auto [confirm_ack_first, confirm_ack_first_status] = nano::deserialize_message_payload (payload_of (*confirm_ack_bytes), confirm_ack.header, network, &filter);
	ASSERT_NE (confirm_ack_first, nullptr);
	filter.clear (dynamic_cast<nano::messages::confirm_ack &> (*confirm_ack_first).digest);
	auto [confirm_ack_again, confirm_ack_again_status] = nano::deserialize_message_payload (payload_of (*confirm_ack_bytes), confirm_ack.header, network, &filter);
	ASSERT_NE (confirm_ack_again, nullptr);
	ASSERT_EQ (confirm_ack_again_status, nano::deserialize_message_status::success);

	// Without a filter there is no digest to clear
	auto [publish_unfiltered, publish_unfiltered_status] = nano::deserialize_message_payload (payload_of (*publish_bytes), publish.header, network);
	ASSERT_NE (publish_unfiltered, nullptr);
	ASSERT_EQ (dynamic_cast<nano::messages::publish &> (*publish_unfiltered).digest, 0);
	auto [confirm_ack_unfiltered, confirm_ack_unfiltered_status] = nano::deserialize_message_payload (payload_of (*confirm_ack_bytes), confirm_ack.header, network);
	ASSERT_NE (confirm_ack_unfiltered, nullptr);
	ASSERT_EQ (dynamic_cast<nano::messages::confirm_ack &> (*confirm_ack_unfiltered).digest, 0);
}

/*
 * Uniquers make a repeated block or vote share one instance, as they do for the same block or vote arriving from many peers
 */
TEST (deserialize_message_payload, uniquers)
{
	auto & network = nano::dev::network_params.network;
	nano::work_pool pool{ network, std::numeric_limits<unsigned>::max () };

	nano::messages::publish const publish{ network, make_block (pool) };
	auto const publish_bytes = publish.to_bytes ();
	auto parse_block = [&] (nano::block_uniquer * uniquer) -> std::shared_ptr<nano::block> {
		auto [message, status] = nano::deserialize_message_payload (payload_of (*publish_bytes), publish.header, network, nullptr, uniquer);
		return message ? dynamic_cast<nano::messages::publish &> (*message).block : nullptr;
	};
	nano::block_uniquer block_uniquer;
	auto const block = parse_block (&block_uniquer);
	ASSERT_NE (block, nullptr);
	ASSERT_EQ (parse_block (&block_uniquer), block);
	auto const block_unshared = parse_block (nullptr);
	ASSERT_NE (block_unshared, nullptr);
	ASSERT_NE (block_unshared, block);

	nano::messages::confirm_ack const confirm_ack{ network, nano::test::make_vote (nano::keypair{}, random_hashes (1)) };
	auto const confirm_ack_bytes = confirm_ack.to_bytes ();
	auto parse_vote = [&] (nano::vote_uniquer * uniquer) -> std::shared_ptr<nano::vote> {
		auto [message, status] = nano::deserialize_message_payload (payload_of (*confirm_ack_bytes), confirm_ack.header, network, nullptr, nullptr, uniquer);
		return message ? dynamic_cast<nano::messages::confirm_ack &> (*message).vote : nullptr;
	};
	nano::vote_uniquer vote_uniquer;
	auto const vote = parse_vote (&vote_uniquer);
	ASSERT_NE (vote, nullptr);
	ASSERT_EQ (parse_vote (&vote_uniquer), vote);
	auto const vote_unshared = parse_vote (nullptr);
	ASSERT_NE (vote_unshared, nullptr);
	ASSERT_NE (vote_unshared, vote);
}

/*
 * A published block without enough work is rejected
 */
TEST (deserialize_message_payload, insufficient_work)
{
	auto const & network = nano::dev::network_params.network;
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (0)
				 .destination (1)
				 .balance (20)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	ASSERT_TRUE (network.work.validate_entry (*block)); // Work zero is not enough for this block
	nano::messages::publish const publish{ network, block };
	auto const bytes = publish.to_bytes ();

	auto [message, status] = nano::deserialize_message_payload (payload_of (*bytes), publish.header, network);
	ASSERT_EQ (message, nullptr);
	ASSERT_EQ (status, nano::deserialize_message_status::insufficient_work);
}

/*
 * Regression test for a remotely-triggerable null-pointer dereference (DoS)
 */
TEST (deserialize_message_payload, publish_not_a_block_does_not_crash)
{
	nano::network_filter filter{ 1 };
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer vote_uniquer;

	// Craft the malicious header: type = publish, block-type nibble = not_a_block
	nano::messages::message_header header{ nano::dev::network_params.network, nano::messages::message_type::publish };
	header.block_type_set (nano::block_type::not_a_block);

	std::vector<uint8_t> empty_payload;
	nano::buffer_view payload{ empty_payload.data (), empty_payload.size () };

	auto [message, status] = nano::deserialize_message_payload (payload, header, nano::dev::network_params.network, &filter, &block_uniquer, &vote_uniquer);

	ASSERT_EQ (message, nullptr);
	ASSERT_EQ (status, nano::deserialize_message_status::invalid_publish_message);
}

/*
 * A confirmation request must name at least one root and hash, so a count of zero is rejected
 */
TEST (deserialize_message_payload, confirm_req_empty)
{
	auto const & network = nano::dev::network_params.network;
	nano::messages::message_header header{ network, nano::messages::message_type::confirm_req };
	header.block_type_set (nano::block_type::not_a_block);
	header.count_set (0);
	ASSERT_EQ (header.payload_length_bytes (), 0);
	auto const bytes = serialize_header (header);

	auto [message, status] = nano::deserialize_message_payload (payload_of (bytes), header, network);
	ASSERT_EQ (message, nullptr);
	ASSERT_EQ (status, nano::deserialize_message_status::invalid_confirm_req_message);
}

/*
 * A zero root and hash pair is skipped, which leaves fewer pairs than the header counts, so the request is rejected
 */
TEST (deserialize_message_payload, confirm_req_zero_pair)
{
	auto const & network = nano::dev::network_params.network;
	std::vector<std::pair<nano::block_hash, nano::root>> const roots_hashes{ { nano::test::random_hash (), nano::test::random_hash () }, { nano::block_hash (0), nano::root (0) } };
	nano::messages::confirm_req const confirm_req{ network, roots_hashes };
	auto const bytes = confirm_req.to_bytes ();

	auto [message, status] = nano::deserialize_message_payload (payload_of (*bytes), confirm_req.header, network);
	ASSERT_EQ (message, nullptr);
	ASSERT_EQ (status, nano::deserialize_message_status::invalid_confirm_req_message);
}

/*
 * An ascending bootstrap request or response naming a payload type the node does not know is rejected
 */
TEST (deserialize_message_payload, asc_pull_unknown_type)
{
	auto const & network = nano::dev::network_params.network;
	nano::messages::asc_pull_req request{ network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::account_info;
	request.payload = nano::messages::asc_pull_req::account_info_payload{ .target = nano::test::random_hash_or_account (), .target_type = nano::messages::asc_pull_req::hash_type::account };
	request.update_header ();
	nano::messages::asc_pull_ack response{ network };
	response.id = 7;
	response.type = nano::messages::asc_pull_type::account_info;
	response.payload = nano::messages::asc_pull_ack::account_info_payload{ .account = nano::test::random_account () };
	response.update_header ();

	// The payload type is the first byte after the header
	auto parse_as_type = [&network] (nano::messages::message const & message, uint8_t type) {
		auto bytes = *message.to_bytes ();
		bytes[nano::messages::message_header::size] = type;
		return nano::deserialize_message_payload (payload_of (bytes), message.header, network);
	};
	for (uint8_t type : { static_cast<uint8_t> (nano::messages::asc_pull_type::invalid), uint8_t{ 0xff } })
	{
		SCOPED_TRACE (static_cast<int> (type));
		auto [request_message, request_status] = parse_as_type (request, type);
		ASSERT_EQ (request_message, nullptr);
		ASSERT_EQ (request_status, nano::deserialize_message_status::invalid_asc_pull_req_message);

		auto [response_message, response_status] = parse_as_type (response, type);
		ASSERT_EQ (response_message, nullptr);
		ASSERT_EQ (response_status, nano::deserialize_message_status::invalid_asc_pull_ack_message);
	}
}

/*
 * A telemetry payload shorter than the oldest layout is rejected, one between two layouts parses as the older one and keeps the rest as unknown data
 */
TEST (deserialize_message_payload, telemetry_ack_sizes)
{
	auto const & network = nano::dev::network_params.network;
	auto parse_size = [&network] (std::size_t size) {
		nano::messages::message_header header{ network, nano::messages::message_type::telemetry_ack };
		header.extensions = size;
		std::vector<uint8_t> const payload (size, 0);
		return nano::deserialize_message_payload (payload, header, network);
	};
	auto const size_v1 = nano::messages::telemetry_data::size_v1;
	auto const size_v2 = nano::messages::telemetry_data::size_v2;

	auto [short_message, short_status] = parse_size (size_v1 - 1);
	ASSERT_EQ (short_message, nullptr);
	ASSERT_EQ (short_status, nano::deserialize_message_status::invalid_telemetry_ack_message);

	auto [between_message, between_status] = parse_size (size_v2 - 1);
	ASSERT_EQ (between_status, nano::deserialize_message_status::success);
	ASSERT_NE (between_message, nullptr);
	auto const & data = dynamic_cast<nano::messages::telemetry_ack const &> (*between_message).data;
	ASSERT_EQ (data.version, nano::messages::telemetry_data_version::v1);
	ASSERT_EQ (data.unknown_data.size (), size_v2 - 1 - size_v1);
}

/*
 * A timestamp beyond what the clock can hold is rejected instead of overflowing on conversion, the latest one it holds parses
 */
TEST (deserialize_message_payload, telemetry_ack_timestamp_range)
{
	auto const & network = nano::dev::network_params.network;
	nano::messages::telemetry_data data;
	data.timestamp = std::chrono::system_clock::time_point::max ();
	nano::messages::telemetry_ack const latest{ network, data };
	auto const latest_bytes = latest.to_bytes ();

	auto [latest_message, latest_status] = nano::deserialize_message_payload (payload_of (*latest_bytes), latest.header, network);
	ASSERT_EQ (latest_status, nano::deserialize_message_status::success);
	ASSERT_NE (latest_message, nullptr);

	// The timestamp precedes the active difficulty that ends the v1 fields, with every bit set it lies past any clock
	auto beyond_bytes = *latest_bytes;
	auto const timestamp_offset = nano::messages::message_header::size + nano::messages::telemetry_data::size_v1 - 2 * sizeof (uint64_t);
	std::fill_n (beyond_bytes.begin () + timestamp_offset, sizeof (uint64_t), 0xff);
	auto [beyond_message, beyond_status] = nano::deserialize_message_payload (payload_of (beyond_bytes), latest.header, network);
	ASSERT_EQ (beyond_message, nullptr);
	ASSERT_EQ (beyond_status, nano::deserialize_message_status::invalid_telemetry_ack_message);
}

/*
 * A handshake that neither queries nor responds has no payload and parses as such, rejecting it is left to the server
 */
TEST (deserialize_message_payload, node_id_handshake_empty)
{
	auto const & network = nano::dev::network_params.network;
	nano::messages::message_header const header{ network, nano::messages::message_type::node_id_handshake };
	ASSERT_EQ (header.payload_length_bytes (), 0);
	auto const bytes = serialize_header (header);

	auto [message, status] = nano::deserialize_message_payload (payload_of (bytes), header, network);
	ASSERT_EQ (status, nano::deserialize_message_status::success);
	ASSERT_NE (message, nullptr);
	auto const & handshake = dynamic_cast<nano::messages::node_id_handshake const &> (*message);
	ASSERT_FALSE (handshake.query.has_value ());
	ASSERT_FALSE (handshake.response.has_value ());
}

/*
 * A block list cut inside a block keeps the blocks before the cut, since the list ends wherever the payload stops parsing as a block
 */
TEST (deserialize_message_payload, asc_pull_ack_blocks_cut)
{
	auto const & network = nano::dev::network_params.network;
	auto const first = nano::test::random_block ();
	nano::messages::asc_pull_ack response{ network };
	response.id = 11;
	response.type = nano::messages::asc_pull_type::blocks;
	response.payload = nano::messages::asc_pull_ack::blocks_payload{ .blocks = { first, nano::test::random_block () } };
	response.update_header ();
	auto const bytes = response.to_bytes ();

	// Each block is written as its type byte and its body, the cut leaves the second block its type byte and half of its body
	auto const block_size = 1 + nano::block::size (first->type ());
	auto const cut = nano::messages::asc_pull_ack::partial_size + block_size + block_size / 2;
	auto [message, status] = nano::deserialize_message_payload (payload_of (*bytes).first (cut), response.header, network);
	ASSERT_EQ (status, nano::deserialize_message_status::success);
	ASSERT_NE (message, nullptr);
	auto const & blocks = std::get<nano::messages::asc_pull_ack::blocks_payload> (dynamic_cast<nano::messages::asc_pull_ack const &> (*message).payload).blocks;
	ASSERT_EQ (blocks.size (), 1);
	ASSERT_EQ (blocks.front ()->hash (), first->hash ());
}

/*
 * Every sample parses from its serialized bytes into the same message class and serializes back to the same bytes.
 * The samples cover every message type a header may carry, so a new type cannot be added without parsing support.
 */
TEST (deserialize_message, round_trip)
{
	auto const & network = nano::dev::network_params.network;
	std::set<nano::messages::message_type> sampled_types;
	for (auto const & sample : make_samples ())
	{
		SCOPED_TRACE (sample.name);
		auto const bytes = sample.message->to_bytes ();

		auto [message, status] = nano::deserialize_message (*bytes, network);
		ASSERT_EQ (status, nano::deserialize_message_status::success);
		ASSERT_NE (message, nullptr);
		auto const & parsed = *message;
		auto const & original = *sample.message;
		ASSERT_TRUE (typeid (parsed) == typeid (original));
		ASSERT_EQ (*message->to_bytes (), *bytes);

		sampled_types.insert (sample.message->type ());
	}
	for (auto type : nano::enum_values<nano::messages::message_type> ())
	{
		nano::messages::message_header const header{ network, type };
		EXPECT_EQ (sampled_types.contains (type), header.is_valid_message_type ()) << nano::messages::to_string (type);
	}
}

/*
 * A buffer holding more or less than the payload length its header declares is rejected
 */
TEST (deserialize_message, payload_length_mismatch)
{
	auto const & network = nano::dev::network_params.network;
	for (auto const & sample : make_samples ())
	{
		SCOPED_TRACE (sample.name);
		auto bytes = *sample.message->to_bytes ();

		bytes.push_back (0);
		auto [longer, longer_status] = nano::deserialize_message (bytes, network);
		ASSERT_EQ (longer, nullptr);
		ASSERT_EQ (longer_status, nano::deserialize_message_status::payload_length_mismatch);

		bytes.resize (bytes.size () - 2);
		if (bytes.size () < nano::messages::message_header::size)
		{
			continue; // Without a payload, the missing byte is part of the header
		}
		auto [shorter, shorter_status] = nano::deserialize_message (bytes, network);
		ASSERT_EQ (shorter, nullptr);
		ASSERT_EQ (shorter_status, nano::deserialize_message_status::payload_length_mismatch);
	}
}

/*
 * A message whose header fails validation is rejected with the header's status
 */
TEST (deserialize_message, invalid_header)
{
	auto const & network = nano::dev::network_params.network;
	nano::messages::keepalive const keepalive{ network };
	auto bytes = *keepalive.to_bytes ();

	auto [short_message, short_status] = nano::deserialize_message (nano::buffer_view{ bytes }.first (nano::messages::message_header::size - 1), network);
	ASSERT_EQ (short_message, nullptr);
	ASSERT_EQ (short_status, nano::deserialize_message_status::invalid_header);

	nano::messages::message_header header{ network, nano::messages::message_type::keepalive };
	header.network = nano::network_type::nano_live_network;
	auto const header_bytes = serialize_header (header);
	std::copy (header_bytes.begin (), header_bytes.end (), bytes.begin ());
	auto [foreign_message, foreign_status] = nano::deserialize_message (bytes, network);
	ASSERT_EQ (foreign_message, nullptr);
	ASSERT_EQ (foreign_status, nano::deserialize_message_status::invalid_network);
}

/*
 * A publish whose header names no block is rejected before its payload length, which comes from the block type, is computed
 */
TEST (deserialize_message, publish_not_a_block)
{
	auto const & network = nano::dev::network_params.network;
	nano::messages::message_header header{ network, nano::messages::message_type::publish };
	header.block_type_set (nano::block_type::not_a_block);

	auto [message, status] = nano::deserialize_message (serialize_header (header), network);
	ASSERT_EQ (message, nullptr);
	ASSERT_EQ (status, nano::deserialize_message_status::invalid_header);
}
