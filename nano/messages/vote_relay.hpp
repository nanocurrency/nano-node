#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/messages/fwd.hpp>
#include <nano/messages/message.hpp>

#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace nano::messages
{
/*
 * Request for known votes on a set of blocks, served by peers advertising the vote_relay capability.
 *
 * Binary Format:
 * [message_header] Common message header
 * [8 bytes] Request id (big endian)
 * [1 byte] Flags
 * - [0x01] Include non-final votes (voting nodes), otherwise only final votes are served
 * [1 byte] Count of (block hash, root) pairs
 * [N x (32 bytes (block hash) + 32 bytes (root))] Pairs of (block_hash, root)
 * [1 byte] Count of requested representatives
 * [M x 32 bytes] Representatives whose votes are wanted, votes from other representatives are not served
 * - An empty list means votes from any representative are wanted
 *
 * Header extensions:
 * - [0xffff] Payload length in bytes
 */
class vote_relay_req final : public message
{
public:
	using id_t = uint64_t;

	explicit vote_relay_req (nano::network_constants const &);
	// An empty list of requested representatives means votes from any representative are wanted
	vote_relay_req (nano::network_constants const &, id_t, std::vector<std::pair<nano::block_hash, nano::root>> const &, std::vector<nano::account> const & reps = {}, bool include_non_final = false);
	vote_relay_req (bool & error, nano::stream &, message_header const &);

	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (message_visitor &) const override;

	static std::size_t size (message_header const &);

	// Update payload size stored in header
	// IMPORTANT: Must be called after any update to the payload
	void update_header ();

	constexpr static std::size_t max_hashes = 255;
	constexpr static std::size_t max_reps = 255;

	// Whether non-final votes are wanted too (voting nodes), otherwise only final votes are served
	static uint8_t constexpr flag_include_non_final = 1 << 0;
	bool include_non_final () const;

private:
	std::size_t payload_size () const;

public: // Payload
	id_t id{ 0 };
	uint8_t flags{ 0 };
	std::vector<std::pair<nano::block_hash, nano::root>> roots_hashes;
	std::vector<nano::account> reps; // Representatives whose votes are wanted, an empty list means votes from any representative are wanted

public: // Logging
	void operator() (nano::object_stream &) const override;
};

/*
 * Response carrying a batch of votes for a previously issued vote relay request.
 *
 * Binary Format:
 * [message_header] Common message header
 * [8 bytes] Request id (big endian)
 * [1 byte] Count of votes
 * [N x (1 byte (hash count) + vote)] Votes prefixed with their hash count
 * - Each vote is serialized/deserialized by the `nano::vote` class.
 *
 * Header extensions:
 * - [0xffff] Payload length in bytes
 */
class vote_relay_ack final : public message
{
public:
	using id_t = vote_relay_req::id_t;

	explicit vote_relay_ack (nano::network_constants const &);
	vote_relay_ack (nano::network_constants const &, id_t, std::vector<std::shared_ptr<nano::vote>> const &);
	vote_relay_ack (bool & error, nano::stream &, message_header const &, nano::vote_uniquer * = nullptr);

	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &, nano::vote_uniquer * = nullptr);
	void visit (message_visitor &) const override;

	static std::size_t size (message_header const &);

	// Update payload size stored in header
	// IMPORTANT: Must be called after any update to the payload
	void update_header ();

	constexpr static std::size_t max_votes = 64;

	// Size of a single vote on the wire, including the hash count prefix
	static std::size_t vote_size (std::shared_ptr<nano::vote> const &);
	// Maximum total payload size that fits the 16 bit length field
	constexpr static std::size_t max_payload = std::numeric_limits<uint16_t>::max ();

private:
	std::size_t payload_size () const;

public: // Payload
	id_t id{ 0 };
	std::vector<std::shared_ptr<nano::vote>> votes;

public: // Logging
	void operator() (nano::object_stream &) const override;
};
}
