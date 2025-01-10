#pragma once

#include <celerix/boost/asio/ip/tcp.hpp>
#include <celerix/lib/asio.hpp>
#include <celerix/lib/config.hpp>
#include <celerix/lib/errors.hpp>
#include <celerix/lib/fwd.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/memory.hpp>
#include <celerix/lib/network_filter.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/object_stream.hpp>
#include <celerix/lib/stats_enums.hpp>
#include <celerix/node/endpoint.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/fwd.hpp>

#include <bitset>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace celerix
{
using block_uniquer = uniquer<celerix::uint256_union, celerix::block>;
using vote_uniquer = uniquer<celerix::block_hash, celerix::vote>;
}

namespace celerix
{
/**
 * Message types are serialized to the network and existing values must thus never change as
 * types are added, removed and reordered in the enum.
 */
enum class message_type : uint8_t
{
	invalid = 0x0,
	not_a_type = 0x1,
	keepalive = 0x2,
	publish = 0x3,
	confirm_req = 0x4,
	confirm_ack = 0x5,
	bulk_pull = 0x6,
	bulk_push = 0x7,
	frontier_req = 0x8,
	/* deleted 0x9 */
	node_id_handshake = 0x0a,
	bulk_pull_account = 0x0b,
	telemetry_req = 0x0c,
	telemetry_ack = 0x0d,
	asc_pull_req = 0x0e,
	asc_pull_ack = 0x0f,
};

std::string_view to_string (celerix::message_type);
stat::detail to_stat_detail (celerix::message_type);
log::detail to_log_detail (celerix::message_type);

enum class bulk_pull_account_flags : uint8_t
{
	pending_hash_and_amount = 0x0,
	pending_address_only = 0x1,
	pending_hash_amount_and_address = 0x2
};

class message_visitor;

/*
 * Common Header Binary Format:
 * [2 bytes] Network (big endian)
 * [1 byte] Maximum protocol version
 * [1 byte] Protocol version currently in use
 * [1 byte] Minimum protocol version
 * [1 byte] Message type
 * [2 bytes] Extensions (message-specific flags and properties)
 *
 * Notes:
 * - The structure and bit usage of the `extensions` field vary by message type.
 */
class message_header final
{
public:
	using extensions_bitset_t = std::bitset<16>;

	message_header (celerix::network_constants const &, celerix::message_type);
	message_header (bool &, celerix::stream &);

	void serialize (celerix::stream &) const;
	bool deserialize (celerix::stream &);

public: // Payload
	celerix::networks network;
	uint8_t version_max;
	uint8_t version_using;
	uint8_t version_min;
	celerix::message_type type;
	extensions_bitset_t extensions;

public:
	static std::size_t constexpr size = sizeof (celerix::networks) + sizeof (version_max) + sizeof (version_using) + sizeof (version_min) + sizeof (type) + sizeof (/* extensions */ uint16_t);

	bool flag_test (uint8_t flag) const;
	void flag_set (uint8_t flag, bool enable = true);

	celerix::block_type block_type () const;
	void block_type_set (celerix::block_type);

	uint8_t count_get () const;
	void count_set (uint8_t);
	uint8_t count_v2_get () const;
	void count_v2_set (uint8_t);

	static uint8_t constexpr bulk_pull_count_present_flag = 0;
	static uint8_t constexpr bulk_pull_ascending_flag = 1;
	bool bulk_pull_is_count_present () const;
	bool bulk_pull_ascending () const;

	static uint8_t constexpr frontier_req_only_confirmed = 1;
	bool frontier_req_is_only_confirmed_present () const;

	static uint8_t constexpr confirm_v2_flag = 0;
	bool confirm_is_v2 () const;
	void confirm_set_v2 (bool);

	/** Size of the payload in bytes. For some messages, the payload size is based on header flags. */
	std::size_t payload_length_bytes () const;
	bool is_valid_message_type () const;

	static extensions_bitset_t constexpr block_type_mask{ 0x0f00 };
	static extensions_bitset_t constexpr count_mask{ 0xf000 };
	static extensions_bitset_t constexpr count_v2_mask_left{ 0xf000 };
	static extensions_bitset_t constexpr count_v2_mask_right{ 0x00f0 };
	static extensions_bitset_t constexpr telemetry_size_mask{ 0x3ff };

public: // Logging
	void operator() (celerix::object_stream &) const;
};

class message
{
public:
	explicit message (celerix::network_constants const &, celerix::message_type);
	explicit message (celerix::message_header const &);
	virtual ~message () = default;

	virtual void serialize (celerix::stream &) const = 0;
	virtual void visit (celerix::message_visitor &) const = 0;
	std::shared_ptr<std::vector<uint8_t>> to_bytes () const;
	celerix::shared_const_buffer to_shared_const_buffer () const;

	celerix::message_type type () const;

public:
	celerix::message_header header;

public: // Logging
	virtual void operator() (celerix::object_stream &) const;
};

/*
 * Binary Format:
 * [message_header] Common message header
 * [8x (16 bytes (IP) + 2 bytes (port)] Array of 8 peers
 *
 * Header extensions:
 * - No specific bits from the `extensions` field are used for `keepalive`.
 */
class keepalive final : public message
{
public:
	explicit keepalive (celerix::network_constants const & constants);
	keepalive (bool &, celerix::stream &, celerix::message_header const &);
	void visit (celerix::message_visitor &) const override;
	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	bool operator== (celerix::keepalive const &) const;
	std::array<celerix::endpoint, 8> peers;
	static std::size_t constexpr size = 8 * (16 + 2);

public: // Logging
	void operator() (celerix::object_stream &) const override;
};

/*
 * Binary Format:
 * [message_header] Common message header
 * [variable] Block (serialized according to the block type specified in the header)
 *
 * Header extensions:
 * - [0x0f00] Block type: Identifies the specific type of the block.
 * - [0x0004] Originator flag
 */
class publish final : public message
{
public:
	publish (bool &, celerix::stream &, celerix::message_header const &, celerix::network_filter::digest_t const & digest = 0, celerix::block_uniquer * = nullptr);
	publish (celerix::network_constants const & constants, std::shared_ptr<celerix::block> const &, bool is_originator = false);

	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &, celerix::block_uniquer * = nullptr);
	void visit (celerix::message_visitor &) const override;
	bool operator== (celerix::publish const &) const;

	static uint8_t constexpr originator_flag = 2; // 0x0004
	bool is_originator () const;

public: // Payload
	std::shared_ptr<celerix::block> block;

	// Messages deserialized from network should have their digest set
	celerix::network_filter::digest_t digest{ 0 };

public: // Logging
	void operator() (celerix::object_stream &) const override;
};

/*
 * Binary Format:
 * [message_header] Common message header
 * [N x (32 bytes (block hash) + 32 bytes (root))] Pairs of (block_hash, root)
 * - The count is determined by the header's count bits.
 *
 * Header extensions:
 * - [0xf000] Count (for V1 protocol)
 * - [0x0f00] Block type
 *   - Not used anymore (V25.1+), but still present and set to `not_a_block = 0x1` for backwards compatibility
 * - [0xf000 (high), 0x00f0 (low)] Count V2 (for V2 protocol)
 * - [0x0001] Confirm V2 flag
 * - [0x0002] Reserved for V3+ versioning
 */
class confirm_req final : public message
{
public:
	confirm_req (bool & error, celerix::stream &, celerix::message_header const &);
	confirm_req (celerix::network_constants const & constants, std::vector<std::pair<celerix::block_hash, celerix::root>> const &);
	confirm_req (celerix::network_constants const & constants, celerix::block_hash const &, celerix::root const &);

	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	void visit (celerix::message_visitor &) const override;
	bool operator== (celerix::confirm_req const &) const;
	std::string roots_string () const;

	static std::size_t size (celerix::message_header const &);

private:
	static uint8_t hash_count (celerix::message_header const &);

public: // Payload
	std::vector<std::pair<celerix::block_hash, celerix::root>> roots_hashes;

public: // Logging
	void operator() (celerix::object_stream &) const override;
};

/*
 * Binary Format:
 * [message_header] Common message header
 * [variable] Vote
 * - Serialized/deserialized by the `celerix::vote` class.
 *
 * Header extensions:
 * - [0xf000] Count (for V1 protocol)
 * - [0x0f00] Block type
 *   - Not used anymore (V25.1+), but still present and set to `not_a_block = 0x1` for backwards compatibility
 * - [0xf000 (high), 0x00f0 (low)] Count V2 masks (for V2 protocol)
 * - [0x0001] Confirm V2 flag
 * - [0x0002] Reserved for V3+ versioning
 * - [0x0004] Rebroadcasted flag
 */
class confirm_ack final : public message
{
public:
	confirm_ack (bool & error, celerix::stream &, celerix::message_header const &, celerix::network_filter::digest_t const & digest = 0, celerix::vote_uniquer * = nullptr);
	confirm_ack (celerix::network_constants const & constants, std::shared_ptr<celerix::vote> const &, bool rebroadcasted = false);

	void serialize (celerix::stream &) const override;
	void visit (celerix::message_visitor &) const override;
	bool operator== (celerix::confirm_ack const &) const;

	static std::size_t size (celerix::message_header const &);

	static uint8_t constexpr rebroadcasted_flag = 2; // 0x0004
	bool is_rebroadcasted () const;

private:
	static uint8_t hash_count (celerix::message_header const &);

public: // Payload
	std::shared_ptr<celerix::vote> vote;

	// Messages deserialized from network should have their digest set
	celerix::network_filter::digest_t digest{ 0 };

public: // Logging
	void operator() (celerix::object_stream &) const override;
};

class frontier_req final : public message
{
public:
	explicit frontier_req (celerix::network_constants const & constants);
	frontier_req (bool &, celerix::stream &, celerix::message_header const &);
	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	void visit (celerix::message_visitor &) const override;
	bool operator== (celerix::frontier_req const &) const;
	celerix::account start;
	uint32_t age;
	uint32_t count;
	static std::size_t constexpr size = sizeof (start) + sizeof (age) + sizeof (count);

public: // Logging
	void operator() (celerix::object_stream &) const override;
};

enum class telemetry_maker : uint8_t
{
	nf_node = 0,
	nf_pruned_node = 1
};

class telemetry_data
{
public:
	celerix::signature signature{ 0 };
	celerix::account node_id{};
	uint64_t block_count{ 0 };
	uint64_t cemented_count{ 0 };
	uint64_t unchecked_count{ 0 };
	uint64_t account_count{ 0 };
	uint64_t bandwidth_cap{ 0 };
	uint64_t uptime{ 0 };
	uint32_t peer_count{ 0 };
	uint8_t protocol_version{ 0 };
	celerix::block_hash genesis_block{ 0 };
	uint8_t major_version{ 0 };
	uint8_t minor_version{ 0 };
	uint8_t patch_version{ 0 };
	uint8_t pre_release_version{ 0 };
	uint8_t maker{ static_cast<std::underlying_type_t<telemetry_maker>> (telemetry_maker::nf_node) }; // Where this telemetry information originated
	std::chrono::system_clock::time_point timestamp;
	uint64_t active_difficulty{ 0 };
	std::vector<uint8_t> unknown_data;

	void serialize (celerix::stream &) const;
	void deserialize (celerix::stream &, uint16_t);
	celerix::error serialize_json (celerix::jsonconfig &, bool) const;
	celerix::error deserialize_json (celerix::jsonconfig &, bool);
	void sign (celerix::keypair const &);
	bool validate_signature () const;
	bool operator== (celerix::telemetry_data const &) const;
	bool operator!= (celerix::telemetry_data const &) const;

	// Size does not include unknown_data
	static auto constexpr size = sizeof (signature) + sizeof (node_id) + sizeof (block_count) + sizeof (cemented_count) + sizeof (unchecked_count) + sizeof (account_count) + sizeof (bandwidth_cap) + sizeof (peer_count) + sizeof (protocol_version) + sizeof (uptime) + sizeof (genesis_block) + sizeof (major_version) + sizeof (minor_version) + sizeof (patch_version) + sizeof (pre_release_version) + sizeof (maker) + sizeof (uint64_t) + sizeof (active_difficulty);
	static auto constexpr latest_size = size; // This needs to be updated for each new telemetry version

private:
	void serialize_without_signature (celerix::stream &) const;

public: // Logging
	void operator() (celerix::object_stream &) const;
};

class telemetry_req final : public message
{
public:
	explicit telemetry_req (celerix::network_constants const & constants);
	explicit telemetry_req (celerix::message_header const &);
	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	void visit (celerix::message_visitor &) const override;

public: // Logging
	void operator() (celerix::object_stream &) const override;
};

class telemetry_ack final : public message
{
public:
	explicit telemetry_ack (celerix::network_constants const & constants);
	telemetry_ack (bool &, celerix::stream &, celerix::message_header const &);
	telemetry_ack (celerix::network_constants const & constants, telemetry_data const &);
	void serialize (celerix::stream &) const override;
	void visit (celerix::message_visitor &) const override;
	bool deserialize (celerix::stream &);
	uint16_t size () const;
	bool is_empty_payload () const;
	static uint16_t size (celerix::message_header const &);
	celerix::telemetry_data data;

public: // Logging
	void operator() (celerix::object_stream &) const override;
};

class bulk_pull final : public message
{
public:
	using count_t = uint32_t;
	explicit bulk_pull (celerix::network_constants const & constants);
	bulk_pull (bool &, celerix::stream &, celerix::message_header const &);
	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	void visit (celerix::message_visitor &) const override;
	celerix::hash_or_account start{ 0 };
	celerix::block_hash end{ 0 };
	count_t count{ 0 };
	bool is_count_present () const;
	void set_count_present (bool);
	static std::size_t constexpr count_present_flag = celerix::message_header::bulk_pull_count_present_flag;
	static std::size_t constexpr extended_parameters_size = 8;
	static std::size_t constexpr size = sizeof (start) + sizeof (end);

public: // Logging
	void operator() (celerix::object_stream &) const override;
};

class bulk_pull_account final : public message
{
public:
	explicit bulk_pull_account (celerix::network_constants const & constants);
	bulk_pull_account (bool &, celerix::stream &, celerix::message_header const &);
	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	void visit (celerix::message_visitor &) const override;
	celerix::account account;
	celerix::amount minimum_amount;
	bulk_pull_account_flags flags;
	static std::size_t constexpr size = sizeof (account) + sizeof (minimum_amount) + sizeof (bulk_pull_account_flags);

public: // Logging
	void operator() (celerix::object_stream &) const override;
};

class bulk_push final : public message
{
public:
	explicit bulk_push (celerix::network_constants const & constants);
	explicit bulk_push (celerix::message_header const &);
	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	void visit (celerix::message_visitor &) const override;

public: // Logging
	void operator() (celerix::object_stream &) const override;
};

class node_id_handshake final : public message
{
public: // Payload definitions
	class query_payload
	{
	public:
		void serialize (celerix::stream &) const;
		void deserialize (celerix::stream &);

		static std::size_t constexpr size = sizeof (celerix::uint256_union);

	public:
		celerix::uint256_union cookie;

	public: // Logging
		void operator() (celerix::object_stream &) const;
	};

	class response_payload
	{
	public:
		void serialize (celerix::stream &) const;
		void deserialize (celerix::stream &, celerix::message_header const &);

		void sign (celerix::uint256_union const & cookie, celerix::keypair const &);
		bool validate (celerix::uint256_union const & cookie) const;

	private:
		std::vector<uint8_t> data_to_sign (celerix::uint256_union const & cookie) const;

	public:
		struct v2_payload
		{
			celerix::uint256_union salt;
			celerix::block_hash genesis;
		};

	public:
		celerix::account node_id;
		celerix::signature signature;
		std::optional<v2_payload> v2;

	public:
		static std::size_t constexpr size_v1 = sizeof (celerix::account) + sizeof (celerix::signature);
		static std::size_t constexpr size_v2 = sizeof (celerix::account) + sizeof (celerix::signature) + sizeof (v2_payload);
		static std::size_t size (celerix::message_header const &);

	public: // Logging
		void operator() (celerix::object_stream &) const;
	};

public:
	explicit node_id_handshake (celerix::network_constants const &, std::optional<query_payload> query = std::nullopt, std::optional<response_payload> response = std::nullopt);
	node_id_handshake (bool &, celerix::stream &, celerix::message_header const &);

	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);

	void visit (celerix::message_visitor &) const override;
	std::size_t size () const;
	static std::size_t size (celerix::message_header const &);

public: // Header
	static uint8_t constexpr query_flag = 0;
	static uint8_t constexpr response_flag = 1;
	static uint8_t constexpr v2_flag = 2;

	static bool is_query (celerix::message_header const &);
	static bool is_response (celerix::message_header const &);
	static bool is_v2 (celerix::message_header const &);
	bool is_v2 () const;

public: // Payload
	std::optional<query_payload> query;
	std::optional<response_payload> response;

public: // Logging
	void operator() (celerix::object_stream &) const override;
};

/**
 * Type of requested asc pull data
 * - blocks:
 * - account_info:
 */
enum class asc_pull_type : uint8_t
{
	invalid = 0x0,
	blocks = 0x1,
	account_info = 0x2,
	frontiers = 0x3,
};

struct empty_payload
{
	void serialize (celerix::stream &) const
	{
		debug_assert (false);
	}
	void deserialize (celerix::stream &)
	{
		debug_assert (false);
	}
	void operator() (celerix::object_stream &) const
	{
		debug_assert (false);
	}
};

/**
 * Ascending bootstrap pull request
 */
class asc_pull_req final : public message
{
public:
	using id_t = uint64_t;

	explicit asc_pull_req (celerix::network_constants const &);
	asc_pull_req (bool & error, celerix::stream &, celerix::message_header const &);

	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	void visit (celerix::message_visitor &) const override;

	static std::size_t size (celerix::message_header const &);

	/**
	 * Update payload size stored in header
	 * IMPORTANT: Must be called after any update to the payload
	 */
	void update_header ();

	void serialize_payload (celerix::stream &) const;
	void deserialize_payload (celerix::stream &);

private: // Debug
	/**
	 * Asserts that payload type is consistent with actual payload
	 */
	bool verify_consistency () const;

public: // Payload definitions
	enum class hash_type : uint8_t
	{
		account = 0,
		block = 1,
	};

	struct blocks_payload
	{
		void serialize (celerix::stream &) const;
		void deserialize (celerix::stream &);

	public: // Payload
		celerix::hash_or_account start{ 0 };
		uint8_t count{ 0 };
		hash_type start_type{};

	public: // Logging
		void operator() (celerix::object_stream &) const;
	};

	struct account_info_payload
	{
		void serialize (celerix::stream &) const;
		void deserialize (celerix::stream &);

	public: // Payload
		celerix::hash_or_account target{ 0 };
		hash_type target_type{};

	public: // Logging
		void operator() (celerix::object_stream &) const;
	};

	struct frontiers_payload
	{
		void serialize (celerix::stream &) const;
		void deserialize (celerix::stream &);

	public: // Payload
		celerix::account start{ 0 };
		uint16_t count{ 0 };

	public: // Logging
		void operator() (celerix::object_stream &) const;
	};

public: // Payload
	asc_pull_type type{ asc_pull_type::invalid };
	id_t id{ 0 };

	/** Payload depends on `asc_pull_type` */
	std::variant<empty_payload, blocks_payload, account_info_payload, frontiers_payload> payload;

public:
	/** Size of message without payload */
	constexpr static std::size_t partial_size = sizeof (type) + sizeof (id);

public: // Logging
	void operator() (celerix::object_stream &) const override;
};

/**
 * Ascending bootstrap pull response
 */
class asc_pull_ack final : public message
{
public:
	using id_t = asc_pull_req::id_t;

	explicit asc_pull_ack (celerix::network_constants const &);
	asc_pull_ack (bool & error, celerix::stream &, celerix::message_header const &);

	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	void visit (celerix::message_visitor &) const override;

	static std::size_t size (celerix::message_header const &);

	/**
	 * Update payload size stored in header
	 * IMPORTANT: Must be called after any update to the payload
	 */
	void update_header ();

	void serialize_payload (celerix::stream &) const;
	void deserialize_payload (celerix::stream &);

private: // Debug
	/**
	 * Asserts that payload type is consistent with actual payload
	 */
	bool verify_consistency () const;

public: // Payload definitions
	struct blocks_payload
	{
		/* Header allows for 16 bit extensions; 65536 bytes / 500 bytes (block size with some future margin) ~ 131 */
		constexpr static std::size_t max_blocks = 128;

		void serialize (celerix::stream &) const;
		void deserialize (celerix::stream &);

	public: // Payload
		std::deque<std::shared_ptr<celerix::block>> blocks;

	public: // Logging
		void operator() (celerix::object_stream &) const;
	};

	struct account_info_payload
	{
		void serialize (celerix::stream &) const;
		void deserialize (celerix::stream &);

	public: // Payload
		celerix::account account{ 0 };
		celerix::block_hash account_open{ 0 };
		celerix::block_hash account_head{ 0 };
		uint64_t account_block_count{ 0 };
		celerix::block_hash account_conf_frontier{ 0 };
		uint64_t account_conf_height{ 0 };

	public: // Logging
		void operator() (celerix::object_stream &) const;
	};

	struct frontiers_payload
	{
		/* Header allows for 16 bit extensions; 65536 bytes / 64 bytes (account + frontier) ~ 1024, but we need some space for null frontier terminator */
		constexpr static std::size_t max_frontiers = 1000;

		using frontier = std::pair<celerix::account, celerix::block_hash>;

		void serialize (celerix::stream &) const;
		void deserialize (celerix::stream &);

		static void serialize_frontier (celerix::stream &, frontier const &);
		static frontier deserialize_frontier (celerix::stream &);

	public: // Payload
		std::deque<frontier> frontiers;

	public: // Logging
		void operator() (celerix::object_stream &) const;
	};

public: // Payload
	asc_pull_type type{ asc_pull_type::invalid };
	id_t id{ 0 };

	/** Payload depends on `asc_pull_type` */
	std::variant<empty_payload, blocks_payload, account_info_payload, frontiers_payload> payload;

public:
	/** Size of message without payload */
	constexpr static std::size_t partial_size = sizeof (type) + sizeof (id);

public: // Logging
	void operator() (celerix::object_stream &) const override;
};

class message_visitor
{
public:
	virtual ~message_visitor () = default;

	virtual void keepalive (celerix::keepalive const & message)
	{
		default_handler (message);
	};
	virtual void publish (celerix::publish const & message)
	{
		default_handler (message);
	}
	virtual void confirm_req (celerix::confirm_req const & message)
	{
		default_handler (message);
	}
	virtual void confirm_ack (celerix::confirm_ack const & message)
	{
		default_handler (message);
	}
	virtual void bulk_pull (celerix::bulk_pull const & message)
	{
		default_handler (message);
	}
	virtual void bulk_pull_account (celerix::bulk_pull_account const & message)
	{
		default_handler (message);
	}
	virtual void bulk_push (celerix::bulk_push const & message)
	{
		default_handler (message);
	}
	virtual void frontier_req (celerix::frontier_req const & message)
	{
		default_handler (message);
	}
	virtual void node_id_handshake (celerix::node_id_handshake const & message)
	{
		default_handler (message);
	}
	virtual void telemetry_req (celerix::telemetry_req const & message)
	{
		default_handler (message);
	}
	virtual void telemetry_ack (celerix::telemetry_ack const & message)
	{
		default_handler (message);
	}
	virtual void asc_pull_req (celerix::asc_pull_req const & message)
	{
		default_handler (message);
	}
	virtual void asc_pull_ack (celerix::asc_pull_ack const & message)
	{
		default_handler (message);
	}
	virtual void default_handler (celerix::message const &){};
};
}
