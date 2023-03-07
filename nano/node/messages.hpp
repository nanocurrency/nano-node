#pragma once

#include <nano/boost/asio/ip/tcp.hpp>
#include <nano/boost/asio/ip/udp.hpp>
#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/asio.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/memory.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/common.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/network_filter.hpp>

#include <bitset>
#include <variant>

namespace nano
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

std::string to_string (message_type);
stat::detail to_stat_detail (message_type);

enum class bulk_pull_account_flags : uint8_t
{
	pending_hash_and_amount = 0x0,
	pending_address_only = 0x1,
	pending_hash_amount_and_address = 0x2
};

class message_visitor;

class message_header final
{
public:
	message_header (nano::network_constants const &, nano::message_type);
	message_header (bool &, nano::stream &);
	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);
	nano::block_type block_type () const;
	void block_type_set (nano::block_type);
	uint8_t count_get () const;
	void count_set (uint8_t);
	nano::networks network;
	uint8_t version_max;
	uint8_t version_using;
	uint8_t version_min;
	std::string to_string () const;

public:
	nano::message_type type;
	std::bitset<16> extensions;
	static std::size_t constexpr size = sizeof (nano::networks) + sizeof (version_max) + sizeof (version_using) + sizeof (version_min) + sizeof (type) + sizeof (/* extensions */ uint16_t);

	void flag_set (uint8_t, bool enable = true);
	static uint8_t constexpr bulk_pull_count_present_flag = 0;
	static uint8_t constexpr bulk_pull_ascending_flag = 1;
	bool bulk_pull_is_count_present () const;
	bool bulk_pull_ascending () const;
	static uint8_t constexpr frontier_req_only_confirmed = 1;
	bool frontier_req_is_only_confirmed_present () const;

	/** Size of the payload in bytes. For some messages, the payload size is based on header flags. */
	std::size_t payload_length_bytes () const;
	bool is_valid_message_type () const;

	static std::bitset<16> constexpr block_type_mask{ 0x0f00 };
	static std::bitset<16> constexpr count_mask{ 0xf000 };
	static std::bitset<16> constexpr telemetry_size_mask{ 0x3ff };
};

class message
{
public:
	explicit message (nano::network_constants const &, nano::message_type);
	explicit message (nano::message_header const &);
	virtual ~message () = default;

	virtual void serialize (nano::stream &) const = 0;
	virtual void visit (nano::message_visitor &) const = 0;
	std::shared_ptr<std::vector<uint8_t>> to_bytes () const;
	nano::shared_const_buffer to_shared_const_buffer () const;

	nano::message_type type () const;

public:
	nano::message_header header;
};

class work_pool;
class network_constants;

class message_parser final
{
public:
	enum class parse_status
	{
		success,
		insufficient_work,
		invalid_header,
		invalid_message_type,
		invalid_keepalive_message,
		invalid_publish_message,
		invalid_confirm_req_message,
		invalid_confirm_ack_message,
		invalid_node_id_handshake_message,
		invalid_telemetry_req_message,
		invalid_telemetry_ack_message,
		outdated_version,
		duplicate_publish_message
	};
	message_parser (nano::network_filter &, nano::block_uniquer &, nano::vote_uniquer &, nano::message_visitor &, nano::work_pool &, nano::network_constants const & protocol);
	void deserialize_buffer (uint8_t const *, std::size_t);
	void deserialize_keepalive (nano::stream &, nano::message_header const &);
	void deserialize_publish (nano::stream &, nano::message_header const &, nano::uint128_t const & = 0);
	void deserialize_confirm_req (nano::stream &, nano::message_header const &);
	void deserialize_confirm_ack (nano::stream &, nano::message_header const &);
	void deserialize_node_id_handshake (nano::stream &, nano::message_header const &);
	void deserialize_telemetry_req (nano::stream &, nano::message_header const &);
	void deserialize_telemetry_ack (nano::stream &, nano::message_header const &);
	bool at_end (nano::stream &);
	nano::network_filter & publish_filter;
	nano::block_uniquer & block_uniquer;
	nano::vote_uniquer & vote_uniquer;
	nano::message_visitor & visitor;
	nano::work_pool & pool;
	parse_status status;
	nano::network_constants const & network;
	std::string status_string ();
	static std::size_t const max_safe_udp_message_size;
};

class keepalive final : public message
{
public:
	explicit keepalive (nano::network_constants const & constants);
	keepalive (bool &, nano::stream &, nano::message_header const &);
	void visit (nano::message_visitor &) const override;
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	bool operator== (nano::keepalive const &) const;
	std::array<nano::endpoint, 8> peers;
	static std::size_t constexpr size = 8 * (16 + 2);
	std::string to_string () const;
};

class publish final : public message
{
public:
	publish (bool &, nano::stream &, nano::message_header const &, nano::uint128_t const & = 0, nano::block_uniquer * = nullptr);
	publish (nano::network_constants const & constants, std::shared_ptr<nano::block> const &);
	void visit (nano::message_visitor &) const override;
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &, nano::block_uniquer * = nullptr);
	bool operator== (nano::publish const &) const;
	std::shared_ptr<nano::block> block;
	nano::uint128_t digest{ 0 };
	std::string to_string () const;
};

class confirm_req final : public message
{
public:
	confirm_req (bool &, nano::stream &, nano::message_header const &, nano::block_uniquer * = nullptr);
	confirm_req (nano::network_constants const & constants, std::shared_ptr<nano::block> const &);
	confirm_req (nano::network_constants const & constants, std::vector<std::pair<nano::block_hash, nano::root>> const &);
	confirm_req (nano::network_constants const & constants, nano::block_hash const &, nano::root const &);
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &, nano::block_uniquer * = nullptr);
	void visit (nano::message_visitor &) const override;
	bool operator== (nano::confirm_req const &) const;
	std::shared_ptr<nano::block> block;
	std::vector<std::pair<nano::block_hash, nano::root>> roots_hashes;
	std::string roots_string () const;
	static std::size_t size (nano::block_type, std::size_t = 0);
	std::string to_string () const;
};

class confirm_ack final : public message
{
public:
	confirm_ack (bool &, nano::stream &, nano::message_header const &, nano::vote_uniquer * = nullptr);
	confirm_ack (nano::network_constants const & constants, std::shared_ptr<nano::vote> const &);
	void serialize (nano::stream &) const override;
	void visit (nano::message_visitor &) const override;
	bool operator== (nano::confirm_ack const &) const;
	static std::size_t size (std::size_t count);
	std::string to_string () const;
	std::shared_ptr<nano::vote> vote;
};

class frontier_req final : public message
{
public:
	explicit frontier_req (nano::network_constants const & constants);
	frontier_req (bool &, nano::stream &, nano::message_header const &);
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (nano::message_visitor &) const override;
	bool operator== (nano::frontier_req const &) const;
	nano::account start;
	uint32_t age;
	uint32_t count;
	static std::size_t constexpr size = sizeof (start) + sizeof (age) + sizeof (count);
	std::string to_string () const;
};

enum class telemetry_maker : uint8_t
{
	nf_node = 0,
	nf_pruned_node = 1
};

class telemetry_data
{
public:
	nano::signature signature{ 0 };
	nano::account node_id{};
	uint64_t block_count{ 0 };
	uint64_t cemented_count{ 0 };
	uint64_t unchecked_count{ 0 };
	uint64_t account_count{ 0 };
	uint64_t bandwidth_cap{ 0 };
	uint64_t uptime{ 0 };
	uint32_t peer_count{ 0 };
	uint8_t protocol_version{ 0 };
	nano::block_hash genesis_block{ 0 };
	uint8_t major_version{ 0 };
	uint8_t minor_version{ 0 };
	uint8_t patch_version{ 0 };
	uint8_t pre_release_version{ 0 };
	uint8_t maker{ static_cast<std::underlying_type_t<telemetry_maker>> (telemetry_maker::nf_node) }; // Where this telemetry information originated
	std::chrono::system_clock::time_point timestamp;
	uint64_t active_difficulty{ 0 };
	std::vector<uint8_t> unknown_data;

	void serialize (nano::stream &) const;
	void deserialize (nano::stream &, uint16_t);
	nano::error serialize_json (nano::jsonconfig &, bool) const;
	nano::error deserialize_json (nano::jsonconfig &, bool);
	void sign (nano::keypair const &);
	bool validate_signature () const;
	bool operator== (nano::telemetry_data const &) const;
	bool operator!= (nano::telemetry_data const &) const;
	std::string to_string () const;

	// Size does not include unknown_data
	static auto constexpr size = sizeof (signature) + sizeof (node_id) + sizeof (block_count) + sizeof (cemented_count) + sizeof (unchecked_count) + sizeof (account_count) + sizeof (bandwidth_cap) + sizeof (peer_count) + sizeof (protocol_version) + sizeof (uptime) + sizeof (genesis_block) + sizeof (major_version) + sizeof (minor_version) + sizeof (patch_version) + sizeof (pre_release_version) + sizeof (maker) + sizeof (uint64_t) + sizeof (active_difficulty);
	static auto constexpr latest_size = size; // This needs to be updated for each new telemetry version
private:
	void serialize_without_signature (nano::stream &) const;
};

class telemetry_req final : public message
{
public:
	explicit telemetry_req (nano::network_constants const & constants);
	explicit telemetry_req (nano::message_header const &);
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (nano::message_visitor &) const override;
	std::string to_string () const;
};

class telemetry_ack final : public message
{
public:
	explicit telemetry_ack (nano::network_constants const & constants);
	telemetry_ack (bool &, nano::stream &, nano::message_header const &);
	telemetry_ack (nano::network_constants const & constants, telemetry_data const &);
	void serialize (nano::stream &) const override;
	void visit (nano::message_visitor &) const override;
	bool deserialize (nano::stream &);
	uint16_t size () const;
	bool is_empty_payload () const;
	std::string to_string () const;
	static uint16_t size (nano::message_header const &);
	nano::telemetry_data data;
};

class bulk_pull final : public message
{
public:
	using count_t = uint32_t;
	explicit bulk_pull (nano::network_constants const & constants);
	bulk_pull (bool &, nano::stream &, nano::message_header const &);
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (nano::message_visitor &) const override;
	nano::hash_or_account start{ 0 };
	nano::block_hash end{ 0 };
	count_t count{ 0 };
	bool is_count_present () const;
	void set_count_present (bool);
	static std::size_t constexpr count_present_flag = nano::message_header::bulk_pull_count_present_flag;
	static std::size_t constexpr extended_parameters_size = 8;
	static std::size_t constexpr size = sizeof (start) + sizeof (end);
	std::string to_string () const;
};

class bulk_pull_account final : public message
{
public:
	explicit bulk_pull_account (nano::network_constants const & constants);
	bulk_pull_account (bool &, nano::stream &, nano::message_header const &);
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (nano::message_visitor &) const override;
	nano::account account;
	nano::amount minimum_amount;
	bulk_pull_account_flags flags;
	static std::size_t constexpr size = sizeof (account) + sizeof (minimum_amount) + sizeof (bulk_pull_account_flags);
	std::string to_string () const;
};

class bulk_push final : public message
{
public:
	explicit bulk_push (nano::network_constants const & constants);
	explicit bulk_push (nano::message_header const &);
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (nano::message_visitor &) const override;
};

class node_id_handshake final : public message
{
public: // Payload definitions
	class query_payload
	{
	public:
		void serialize (nano::stream &) const;
		void deserialize (nano::stream &);

		static std::size_t constexpr size = sizeof (nano::uint256_union);

	public:
		nano::uint256_union cookie;
	};

	class response_payload
	{
	public:
		void serialize (nano::stream &) const;
		void deserialize (nano::stream &, nano::message_header const &);

		void sign (nano::uint256_union const & cookie, nano::keypair const &);
		bool validate (nano::uint256_union const & cookie) const;

	private:
		std::vector<uint8_t> data_to_sign (nano::uint256_union const & cookie) const;

	public:
		struct v2_payload
		{
			nano::uint256_union salt;
			nano::block_hash genesis;
		};

	public:
		nano::account node_id;
		nano::signature signature;
		std::optional<v2_payload> v2;

	public:
		static std::size_t constexpr size_v1 = sizeof (nano::account) + sizeof (nano::signature);
		static std::size_t constexpr size_v2 = sizeof (nano::account) + sizeof (nano::signature) + sizeof (v2_payload);
		static std::size_t size (nano::message_header const &);
	};

public:
	explicit node_id_handshake (nano::network_constants const &, std::optional<query_payload> query = std::nullopt, std::optional<response_payload> response = std::nullopt);
	node_id_handshake (bool &, nano::stream &, nano::message_header const &);

	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);

	void visit (nano::message_visitor &) const override;
	std::size_t size () const;
	static std::size_t size (nano::message_header const &);
	std::string to_string () const;

public: // Header
	static uint8_t constexpr query_flag = 0;
	static uint8_t constexpr response_flag = 1;
	static uint8_t constexpr v2_flag = 2;

	static bool is_query (nano::message_header const &);
	static bool is_response (nano::message_header const &);
	static bool is_v2 (nano::message_header const &);
	bool is_v2 () const;

public: // Payload
	std::optional<query_payload> query;
	std::optional<response_payload> response;
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
};

class empty_payload
{
public:
	void serialize (nano::stream &) const
	{
		debug_assert (false);
	}
	void deserialize (nano::stream &)
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

	explicit asc_pull_req (nano::network_constants const &);
	asc_pull_req (bool & error, nano::stream &, nano::message_header const &);

	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (nano::message_visitor &) const override;

	static std::size_t size (nano::message_header const &);

	/**
	 * Update payload size stored in header
	 * IMPORTANT: Must be called after any update to the payload
	 */
	void update_header ();

	void serialize_payload (nano::stream &) const;
	void deserialize_payload (nano::stream &);

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

	class blocks_payload
	{
	public:
		void serialize (nano::stream &) const;
		void deserialize (nano::stream &);

	public:
		nano::hash_or_account start{ 0 };
		uint8_t count{ 0 };
		asc_pull_req::hash_type start_type{ 0 };
	};

	class account_info_payload
	{
	public:
		void serialize (nano::stream &) const;
		void deserialize (nano::stream &);

	public:
		nano::hash_or_account target{ 0 };
		asc_pull_req::hash_type target_type{ 0 };
	};

public: // Payload
	/** Currently unused, allows extensions in the future */
	asc_pull_type type{ asc_pull_type::invalid };
	id_t id{ 0 };

	/** Payload depends on `asc_pull_type` */
	std::variant<empty_payload, blocks_payload, account_info_payload> payload;

public:
	/** Size of message without payload */
	constexpr static std::size_t partial_size = sizeof (type) + sizeof (id);
};

/**
 * Ascending bootstrap pull response
 */
class asc_pull_ack final : public message
{
public:
	using id_t = asc_pull_req::id_t;

	explicit asc_pull_ack (nano::network_constants const &);
	asc_pull_ack (bool & error, nano::stream &, nano::message_header const &);

	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (nano::message_visitor &) const override;

	static std::size_t size (nano::message_header const &);

	/**
	 * Update payload size stored in header
	 * IMPORTANT: Must be called after any update to the payload
	 */
	void update_header ();

	void serialize_payload (nano::stream &) const;
	void deserialize_payload (nano::stream &);

private: // Debug
	/**
	 * Asserts that payload type is consistent with actual payload
	 */
	bool verify_consistency () const;

public: // Payload definitions
	class blocks_payload
	{
	public:
		void serialize (nano::stream &) const;
		void deserialize (nano::stream &);

	public:
		std::vector<std::shared_ptr<nano::block>> blocks{};

	public:
		/* Header allows for 16 bit extensions; 65535 bytes / 500 bytes (block size with some future margin) ~ 131 */
		constexpr static std::size_t max_blocks = 128;
	};

	class account_info_payload
	{
	public:
		void serialize (nano::stream &) const;
		void deserialize (nano::stream &);

	public:
		nano::account account{ 0 };
		nano::block_hash account_open{ 0 };
		nano::block_hash account_head{ 0 };
		uint64_t account_block_count{ 0 };
		nano::block_hash account_conf_frontier{ 0 };
		uint64_t account_conf_height{ 0 };
	};

public: // Payload
	/** Currently unused, allows extensions in the future */
	asc_pull_type type{ asc_pull_type::invalid };
	id_t id{ 0 };

	/** Payload depends on `asc_pull_type` */
	std::variant<empty_payload, blocks_payload, account_info_payload> payload;

public:
	/** Size of message without payload */
	constexpr static std::size_t partial_size = sizeof (type) + sizeof (id);
};

class message_visitor
{
public:
	virtual ~message_visitor () = default;

	virtual void keepalive (nano::keepalive const & message)
	{
		default_handler (message);
	};
	virtual void publish (nano::publish const & message)
	{
		default_handler (message);
	}
	virtual void confirm_req (nano::confirm_req const & message)
	{
		default_handler (message);
	}
	virtual void confirm_ack (nano::confirm_ack const & message)
	{
		default_handler (message);
	}
	virtual void bulk_pull (nano::bulk_pull const & message)
	{
		default_handler (message);
	}
	virtual void bulk_pull_account (nano::bulk_pull_account const & message)
	{
		default_handler (message);
	}
	virtual void bulk_push (nano::bulk_push const & message)
	{
		default_handler (message);
	}
	virtual void frontier_req (nano::frontier_req const & message)
	{
		default_handler (message);
	}
	virtual void node_id_handshake (nano::node_id_handshake const & message)
	{
		default_handler (message);
	}
	virtual void telemetry_req (nano::telemetry_req const & message)
	{
		default_handler (message);
	}
	virtual void telemetry_ack (nano::telemetry_ack const & message)
	{
		default_handler (message);
	}
	virtual void asc_pull_req (nano::asc_pull_req const & message)
	{
		default_handler (message);
	}
	virtual void asc_pull_ack (nano::asc_pull_ack const & message)
	{
		default_handler (message);
	}
	virtual void default_handler (nano::message const &){};
};
}