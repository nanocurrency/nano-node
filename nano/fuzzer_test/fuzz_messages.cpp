#include <nano/lib/network_filter.hpp>
#include <nano/messages/deserialize_message.hpp>
#include <nano/messages/message.hpp>
#include <nano/messages/uniquers.hpp>
#include <nano/secure/network_params.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace
{
nano::block_uniquer block_uniquer;
nano::vote_uniquer vote_uniquer;
}

/** Parses the buffer the two ways the node does: whole, as the inproc channel does, and header first with whatever of the declared payload length follows, as the TCP server does */
void fuzz_message_parsing (uint8_t const * data, size_t size)
{
	auto const & network = nano::dev::network_params.network;
	nano::buffer_view const buffer{ data, size };

	// A fresh filter per parse keeps each input independent of the ones before it
	nano::network_filter message_filter{ 1 };
	nano::deserialize_message (buffer, network, &message_filter, &block_uniquer, &vote_uniquer);

	auto [header, status] = nano::deserialize_message_header (buffer, network);
	if (header)
	{
		auto const payload = buffer.subspan (nano::messages::message_header::size);
		nano::network_filter payload_filter{ 1 };
		nano::deserialize_message_payload (payload.first (std::min (payload.size (), header->payload_length_bytes ())), *header, network, &payload_filter, &block_uniquer, &vote_uniquer);
	}
}

/** Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput (uint8_t const * data, size_t size)
{
	fuzz_message_parsing (data, size);
	return 0;
}
