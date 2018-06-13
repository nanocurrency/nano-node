#pragma once

#include <banano/common.hpp>
#include <banano/lib/interface.h>

#include <boost/asio.hpp>

#include <bitset>

#include <xxhash/xxhash.h>

namespace rai
{
using endpoint = boost::asio::ip::udp::endpoint;
bool parse_port (std::string const &, uint16_t &);
bool parse_address_port (std::string const &, boost::asio::ip::address &, uint16_t &);
using tcp_endpoint = boost::asio::ip::tcp::endpoint;
bool parse_endpoint (std::string const &, rai::endpoint &);
bool parse_tcp_endpoint (std::string const &, rai::tcp_endpoint &);
bool reserved_address (rai::endpoint const &);
}
static uint64_t endpoint_hash_raw (rai::endpoint const & endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
	rai::uint128_union address;
	address.bytes = endpoint_a.address ().to_v6 ().to_bytes ();
	XXH64_state_t hash;
	XXH64_reset (&hash, 0);
	XXH64_update (&hash, address.bytes.data (), address.bytes.size ());
	auto port (endpoint_a.port ());
	XXH64_update (&hash, &port, sizeof (port));
	auto result (XXH64_digest (&hash));
	return result;
}

namespace std
{
template <size_t size>
struct endpoint_hash
{
};
template <>
struct endpoint_hash<8>
{
	size_t operator() (rai::endpoint const & endpoint_a) const
	{
		return endpoint_hash_raw (endpoint_a);
	}
};
template <>
struct endpoint_hash<4>
{
	size_t operator() (rai::endpoint const & endpoint_a) const
	{
		uint64_t big (endpoint_hash_raw (endpoint_a));
		uint32_t result (static_cast<uint32_t> (big) ^ static_cast<uint32_t> (big >> 32));
		return result;
	}
};
template <>
struct hash<rai::endpoint>
{
	size_t operator() (rai::endpoint const & endpoint_a) const
	{
		endpoint_hash<sizeof (size_t)> ehash;
		return ehash (endpoint_a);
	}
};
}
namespace boost
{
template <>
struct hash<rai::endpoint>
{
	size_t operator() (rai::endpoint const & endpoint_a) const
	{
		std::hash<rai::endpoint> hash;
		return hash (endpoint_a);
	}
};
}

namespace rai
{
enum class message_type : uint8_t
{
	invalid,
	not_a_type,
	keepalive,
	publish,
	confirm_req,
	confirm_ack,
	bulk_pull,
	bulk_push,
	frontier_req,
	bulk_pull_blocks
};
enum class bulk_pull_blocks_mode : uint8_t
{
	list_blocks,
	checksum_blocks
};
class message_visitor;
class message_header
{
public:
	message_header (rai::message_type);
	message_header (bool &, rai::stream &);
	void serialize (rai::stream &);
	bool deserialize (rai::stream &);
	rai::block_type block_type () const;
	void block_type_set (rai::block_type);
	bool ipv4_only ();
	void ipv4_only_set (bool);
	static std::array<uint8_t, 2> constexpr magic_number = rai::banano_network == rai::banano_networks::banano_test_network ? std::array<uint8_t, 2> ({ 'B', 'Z' }) : rai::banano_network == rai::banano_networks::banano_beta_network ? std::array<uint8_t, 2> ({ 'B', 'Y' }) : std::array<uint8_t, 2> ({ 'B', 'X' });
	uint8_t version_max;
	uint8_t version_using;
	uint8_t version_min;
	rai::message_type type;
	std::bitset<16> extensions;
	static size_t constexpr ipv4_only_position = 1;
	static size_t constexpr bootstrap_server_position = 2;
	static std::bitset<16> constexpr block_type_mask = std::bitset<16> (0x0f00);
};
class message
{
public:
	message (rai::message_type);
	message (rai::message_header const &);
	virtual ~message () = default;
	virtual void serialize (rai::stream &) = 0;
	virtual bool deserialize (rai::stream &) = 0;
	virtual void visit (rai::message_visitor &) const = 0;
	rai::message_header header;
};
class work_pool;
class message_parser
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
		invalid_confirm_ack_message
	};
	message_parser (rai::message_visitor &, rai::work_pool &);
	void deserialize_buffer (uint8_t const *, size_t);
	void deserialize_keepalive (rai::stream &, rai::message_header const &);
	void deserialize_publish (rai::stream &, rai::message_header const &);
	void deserialize_confirm_req (rai::stream &, rai::message_header const &);
	void deserialize_confirm_ack (rai::stream &, rai::message_header const &);
	bool at_end (rai::stream &);
	rai::message_visitor & visitor;
	rai::work_pool & pool;
	parse_status status;
};
class keepalive : public message
{
public:
	keepalive (bool &, rai::stream &, rai::message_header const &);
	keepalive ();
	void visit (rai::message_visitor &) const override;
	bool deserialize (rai::stream &) override;
	void serialize (rai::stream &) override;
	bool operator== (rai::keepalive const &) const;
	std::array<rai::endpoint, 8> peers;
};
class publish : public message
{
public:
	publish (bool &, rai::stream &, rai::message_header const &);
	publish (std::shared_ptr<rai::block>);
	void visit (rai::message_visitor &) const override;
	bool deserialize (rai::stream &) override;
	void serialize (rai::stream &) override;
	bool operator== (rai::publish const &) const;
	std::shared_ptr<rai::block> block;
};
class confirm_req : public message
{
public:
	confirm_req (bool &, rai::stream &, rai::message_header const &);
	confirm_req (std::shared_ptr<rai::block>);
	bool deserialize (rai::stream &) override;
	void serialize (rai::stream &) override;
	void visit (rai::message_visitor &) const override;
	bool operator== (rai::confirm_req const &) const;
	std::shared_ptr<rai::block> block;
};
class confirm_ack : public message
{
public:
	confirm_ack (bool &, rai::stream &, rai::message_header const &);
	confirm_ack (std::shared_ptr<rai::vote>);
	bool deserialize (rai::stream &) override;
	void serialize (rai::stream &) override;
	void visit (rai::message_visitor &) const override;
	bool operator== (rai::confirm_ack const &) const;
	std::shared_ptr<rai::vote> vote;
};
class frontier_req : public message
{
public:
	frontier_req ();
	frontier_req (bool &, rai::stream &, rai::message_header const &);
	bool deserialize (rai::stream &) override;
	void serialize (rai::stream &) override;
	void visit (rai::message_visitor &) const override;
	bool operator== (rai::frontier_req const &) const;
	rai::account start;
	uint32_t age;
	uint32_t count;
};
class bulk_pull : public message
{
public:
	bulk_pull ();
	bulk_pull (bool &, rai::stream &, rai::message_header const &);
	bool deserialize (rai::stream &) override;
	void serialize (rai::stream &) override;
	void visit (rai::message_visitor &) const override;
	rai::uint256_union start;
	rai::block_hash end;
};
class bulk_pull_blocks : public message
{
public:
	bulk_pull_blocks ();
	bulk_pull_blocks (bool &, rai::stream &, rai::message_header const &);
	bool deserialize (rai::stream &) override;
	void serialize (rai::stream &) override;
	void visit (rai::message_visitor &) const override;
	rai::block_hash min_hash;
	rai::block_hash max_hash;
	bulk_pull_blocks_mode mode;
	uint32_t max_count;
};
class bulk_push : public message
{
public:
	bulk_push ();
	bulk_push (rai::message_header const &);
	bool deserialize (rai::stream &) override;
	void serialize (rai::stream &) override;
	void visit (rai::message_visitor &) const override;
};
class message_visitor
{
public:
	virtual void keepalive (rai::keepalive const &) = 0;
	virtual void publish (rai::publish const &) = 0;
	virtual void confirm_req (rai::confirm_req const &) = 0;
	virtual void confirm_ack (rai::confirm_ack const &) = 0;
	virtual void bulk_pull (rai::bulk_pull const &) = 0;
	virtual void bulk_pull_blocks (rai::bulk_pull_blocks const &) = 0;
	virtual void bulk_push (rai::bulk_push const &) = 0;
	virtual void frontier_req (rai::frontier_req const &) = 0;
	virtual ~message_visitor ();
};

/**
 * Returns seconds passed since unix epoch (posix time)
 */
inline uint64_t seconds_since_epoch ()
{
	return std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
}
}
