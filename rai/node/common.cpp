
#include <rai/node/common.hpp>

#include <rai/lib/work.hpp>
#include <rai/node/wallet.hpp>

std::array<uint8_t, 2> constexpr rai::message_header::magic_number;
size_t constexpr rai::message_header::ipv4_only_position;
size_t constexpr rai::message_header::bootstrap_server_position;
std::bitset<16> constexpr rai::message_header::block_type_mask;

rai::message_header::message_header (rai::message_type type_a) :
version_max (rai::protocol_version),
version_using (rai::protocol_version),
version_min (rai::protocol_version_min),
type (type_a)
{
}

rai::message_header::message_header (bool & error_a, rai::stream & stream_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void rai::message_header::serialize (rai::stream & stream_a)
{
	rai::write (stream_a, rai::message_header::magic_number);
	rai::write (stream_a, version_max);
	rai::write (stream_a, version_using);
	rai::write (stream_a, version_min);
	rai::write (stream_a, type);
	rai::write (stream_a, static_cast<uint16_t> (extensions.to_ullong ()));
}

bool rai::message_header::deserialize (rai::stream & stream_a)
{
	uint16_t extensions_l;
	std::array<uint8_t, 2> magic_number_l;
	auto result (rai::read (stream_a, magic_number_l));
	result = result || magic_number_l != magic_number;
	result = result || rai::read (stream_a, version_max);
	result = result || rai::read (stream_a, version_using);
	result = result || rai::read (stream_a, version_min);
	result = result || rai::read (stream_a, type);
	result = result || rai::read (stream_a, extensions_l);
	if (!result)
	{
		extensions = extensions_l;
	}
	return result;
}

rai::message::message (rai::message_type type_a) :
header (type_a)
{
}

rai::message::message (rai::message_header const & header_a) :
header (header_a)
{
}

rai::block_type rai::message_header::block_type () const
{
	return static_cast<rai::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void rai::message_header::block_type_set (rai::block_type type_a)
{
	extensions &= ~block_type_mask;
	extensions |= std::bitset<16> (static_cast<unsigned long long> (type_a) << 8);
}

bool rai::message_header::ipv4_only ()
{
	return extensions.test (ipv4_only_position);
}

void rai::message_header::ipv4_only_set (bool value_a)
{
	extensions.set (ipv4_only_position, value_a);
}

// MTU - IP header - UDP header
const size_t rai::message_parser::max_safe_udp_message_size = 508;

rai::message_parser::message_parser (rai::message_visitor & visitor_a, rai::work_pool & pool_a) :
visitor (visitor_a),
pool (pool_a),
status (parse_status::success)
{
}

void rai::message_parser::deserialize_buffer (uint8_t const * buffer_a, size_t size_a)
{
	status = parse_status::success;
	auto error (false);
	if (size_a <= max_safe_udp_message_size)
	{
		// Guaranteed to be deliverable
		rai::bufferstream stream (buffer_a, size_a);
		rai::message_header header (error, stream);
		if (!error)
		{
			if (rai::rai_network == rai::rai_networks::rai_beta_network && header.version_using < rai::protocol_version)
			{
				status = parse_status::outdated_version;
			}
			else
			{
				switch (header.type)
				{
					case rai::message_type::keepalive:
					{
						deserialize_keepalive (stream, header);
						break;
					}
					case rai::message_type::publish:
					{
						deserialize_publish (stream, header);
						break;
					}
					case rai::message_type::confirm_req:
					{
						deserialize_confirm_req (stream, header);
						break;
					}
					case rai::message_type::confirm_ack:
					{
						deserialize_confirm_ack (stream, header);
						break;
					}
					case rai::message_type::node_id_handshake:
					{
						deserialize_node_id_handshake (stream, header);
						break;
					}
					case rai::message_type::musig_stage0_req:
					{
						deserialize_musig_stage0_req (stream, header);
						break;
					}
					case rai::message_type::musig_stage0_res:
					{
						deserialize_musig_stage0_res (stream, header);
						break;
					}
					case rai::message_type::musig_stage1_req:
					{
						deserialize_musig_stage1_req (stream, header);
						break;
					}
					case rai::message_type::musig_stage1_res:
					{
						deserialize_musig_stage1_res (stream, header);
						break;
					}
					default:
					{
						status = parse_status::invalid_message_type;
						break;
					}
				}
			}
		}
		else
		{
			status = parse_status::invalid_header;
		}
	}
}

void rai::message_parser::deserialize_keepalive (rai::stream & stream_a, rai::message_header const & header_a)
{
	auto error (false);
	rai::keepalive incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		visitor.keepalive (incoming);
	}
	else
	{
		status = parse_status::invalid_keepalive_message;
	}
}

void rai::message_parser::deserialize_publish (rai::stream & stream_a, rai::message_header const & header_a)
{
	auto error (false);
	rai::publish incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		if (!rai::work_validate (*incoming.block))
		{
			visitor.publish (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_publish_message;
	}
}

void rai::message_parser::deserialize_confirm_req (rai::stream & stream_a, rai::message_header const & header_a)
{
	auto error (false);
	rai::confirm_req incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		if (!rai::work_validate (*incoming.block))
		{
			visitor.confirm_req (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_confirm_req_message;
	}
}

void rai::message_parser::deserialize_confirm_ack (rai::stream & stream_a, rai::message_header const & header_a)
{
	auto error (false);
	rai::confirm_ack incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		for (auto & vote_block : incoming.vote->blocks)
		{
			if (!vote_block.which ())
			{
				auto block (boost::get<std::shared_ptr<rai::block>> (vote_block));
				if (rai::work_validate (*block))
				{
					status = parse_status::insufficient_work;
					break;
				}
			}
		}
		if (status == parse_status::success)
		{
			visitor.confirm_ack (incoming);
		}
	}
	else
	{
		status = parse_status::invalid_confirm_ack_message;
	}
}

void rai::message_parser::deserialize_node_id_handshake (rai::stream & stream_a, rai::message_header const & header_a)
{
	bool error_l (false);
	rai::node_id_handshake incoming (error_l, stream_a, header_a);
	if (!error_l && at_end (stream_a))
	{
		visitor.node_id_handshake (incoming);
	}
	else
	{
		status = parse_status::invalid_node_id_handshake_message;
	}
}

void rai::message_parser::deserialize_musig_stage0_req (rai::stream & stream_a, rai::message_header const & header_a)
{
	bool error_l (false);
	rai::musig_stage0_req incoming (error_l, stream_a, header_a);
	if (!error_l && at_end (stream_a))
	{
		if (!rai::work_validate (*incoming.block))
		{
			visitor.musig_stage0_req (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_musig_stage0_req_message;
	}
}

void rai::message_parser::deserialize_musig_stage0_res (rai::stream & stream_a, rai::message_header const & header_a)
{
	bool error_l (false);
	rai::musig_stage0_res incoming (error_l, stream_a, header_a);
	if (!error_l && at_end (stream_a))
	{
		visitor.musig_stage0_res (incoming);
	}
	else
	{
		status = parse_status::invalid_musig_stage0_res_message;
	}
}

void rai::message_parser::deserialize_musig_stage1_req (rai::stream & stream_a, rai::message_header const & header_a)
{
	bool error_l (false);
	rai::musig_stage1_req incoming (error_l, stream_a, header_a);
	if (!error_l && at_end (stream_a))
	{
		visitor.musig_stage1_req (incoming);
	}
	else
	{
		status = parse_status::invalid_musig_stage1_req_message;
	}
}

void rai::message_parser::deserialize_musig_stage1_res (rai::stream & stream_a, rai::message_header const & header_a)
{
	bool error_l (false);
	rai::musig_stage1_res incoming (error_l, stream_a, header_a);
	if (!error_l && at_end (stream_a))
	{
		visitor.musig_stage1_res (incoming);
	}
	else
	{
		status = parse_status::invalid_musig_stage1_res_message;
	}
}

bool rai::message_parser::at_end (rai::stream & stream_a)
{
	uint8_t junk;
	auto end (rai::read (stream_a, junk));
	return end;
}

rai::keepalive::keepalive () :
message (rai::message_type::keepalive)
{
	rai::endpoint endpoint (boost::asio::ip::address_v6{}, 0);
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
	{
		*i = endpoint;
	}
}

rai::keepalive::keepalive (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void rai::keepalive::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.keepalive (*this);
}

void rai::keepalive::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		assert (i->address ().is_v6 ());
		auto bytes (i->address ().to_v6 ().to_bytes ());
		write (stream_a, bytes);
		write (stream_a, i->port ());
	}
}

bool rai::keepalive::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::keepalive);
	auto error (false);
	for (auto i (peers.begin ()), j (peers.end ()); i != j && !error; ++i)
	{
		std::array<uint8_t, 16> address;
		uint16_t port;
		if (!read (stream_a, address) && !read (stream_a, port))
		{
			*i = rai::endpoint (boost::asio::ip::address_v6 (address), port);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool rai::keepalive::operator== (rai::keepalive const & other_a) const
{
	return peers == other_a.peers;
}

rai::publish::publish (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

rai::publish::publish (std::shared_ptr<rai::block> block_a) :
message (rai::message_type::publish),
block (block_a)
{
	header.block_type_set (block->type ());
}

bool rai::publish::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::publish);
	block = rai::deserialize_block (stream_a, header.block_type ());
	auto result (block == nullptr);
	return result;
}

void rai::publish::serialize (rai::stream & stream_a)
{
	assert (block != nullptr);
	header.serialize (stream_a);
	block->serialize (stream_a);
}

void rai::publish::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.publish (*this);
}

bool rai::publish::operator== (rai::publish const & other_a) const
{
	return *block == *other_a.block;
}

rai::confirm_req::confirm_req (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

rai::confirm_req::confirm_req (std::shared_ptr<rai::block> block_a) :
message (rai::message_type::confirm_req),
block (block_a)
{
	header.block_type_set (block->type ());
}

bool rai::confirm_req::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::confirm_req);
	block = rai::deserialize_block (stream_a, header.block_type ());
	auto result (block == nullptr);
	return result;
}

void rai::confirm_req::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.confirm_req (*this);
}

void rai::confirm_req::serialize (rai::stream & stream_a)
{
	assert (block != nullptr);
	header.serialize (stream_a);
	block->serialize (stream_a);
}

bool rai::confirm_req::operator== (rai::confirm_req const & other_a) const
{
	return *block == *other_a.block;
}

rai::confirm_ack::confirm_ack (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a),
vote (std::make_shared<rai::vote> (error_a, stream_a, header.block_type ()))
{
}

rai::confirm_ack::confirm_ack (std::shared_ptr<rai::vote> vote_a) :
message (rai::message_type::confirm_ack),
vote (vote_a)
{
	auto & first_vote_block (vote_a->blocks[0]);
	if (first_vote_block.which ())
	{
		header.block_type_set (rai::block_type::not_a_block);
	}
	else
	{
		header.block_type_set (boost::get<std::shared_ptr<rai::block>> (first_vote_block)->type ());
	}
}

bool rai::confirm_ack::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::confirm_ack);
	auto result (vote->deserialize (stream_a));
	return result;
}

void rai::confirm_ack::serialize (rai::stream & stream_a)
{
	assert (header.block_type () == rai::block_type::not_a_block || header.block_type () == rai::block_type::send || header.block_type () == rai::block_type::receive || header.block_type () == rai::block_type::open || header.block_type () == rai::block_type::change || header.block_type () == rai::block_type::state);
	header.serialize (stream_a);
	vote->serialize (stream_a, header.block_type ());
}

bool rai::confirm_ack::operator== (rai::confirm_ack const & other_a) const
{
	auto result (*vote == *other_a.vote);
	return result;
}

void rai::confirm_ack::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.confirm_ack (*this);
}

rai::frontier_req::frontier_req () :
message (rai::message_type::frontier_req)
{
}

rai::frontier_req::frontier_req (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

bool rai::frontier_req::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::frontier_req);
	auto result (read (stream_a, start.bytes));
	if (!result)
	{
		result = read (stream_a, age);
		if (!result)
		{
			result = read (stream_a, count);
		}
	}
	return result;
}

void rai::frontier_req::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, start.bytes);
	write (stream_a, age);
	write (stream_a, count);
}

void rai::frontier_req::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.frontier_req (*this);
}

bool rai::frontier_req::operator== (rai::frontier_req const & other_a) const
{
	return start == other_a.start && age == other_a.age && count == other_a.count;
}

rai::bulk_pull::bulk_pull () :
message (rai::message_type::bulk_pull)
{
}

rai::bulk_pull::bulk_pull (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void rai::bulk_pull::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull (*this);
}

bool rai::bulk_pull::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::bulk_pull);
	auto result (read (stream_a, start));
	if (!result)
	{
		result = read (stream_a, end);
	}
	return result;
}

void rai::bulk_pull::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, start);
	write (stream_a, end);
}

rai::bulk_pull_account::bulk_pull_account () :
message (rai::message_type::bulk_pull_account)
{
}

rai::bulk_pull_account::bulk_pull_account (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void rai::bulk_pull_account::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull_account (*this);
}

bool rai::bulk_pull_account::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::bulk_pull_account);
	auto result (read (stream_a, account));
	if (!result)
	{
		result = read (stream_a, minimum_amount);
		if (!result)
		{
			result = read (stream_a, flags);
		}
	}
	return result;
}

void rai::bulk_pull_account::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, account);
	write (stream_a, minimum_amount);
	write (stream_a, flags);
}

rai::bulk_pull_blocks::bulk_pull_blocks () :
message (rai::message_type::bulk_pull_blocks)
{
}

rai::bulk_pull_blocks::bulk_pull_blocks (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void rai::bulk_pull_blocks::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull_blocks (*this);
}

bool rai::bulk_pull_blocks::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::bulk_pull_blocks);
	auto result (read (stream_a, min_hash));
	if (!result)
	{
		result = read (stream_a, max_hash);
		if (!result)
		{
			result = read (stream_a, mode);
			if (!result)
			{
				result = read (stream_a, max_count);
			}
		}
	}
	return result;
}

void rai::bulk_pull_blocks::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, min_hash);
	write (stream_a, max_hash);
	write (stream_a, mode);
	write (stream_a, max_count);
}

rai::bulk_push::bulk_push () :
message (rai::message_type::bulk_push)
{
}

rai::bulk_push::bulk_push (rai::message_header const & header_a) :
message (header_a)
{
}

bool rai::bulk_push::deserialize (rai::stream & stream_a)
{
	assert (header.type == rai::message_type::bulk_push);
	return false;
}

void rai::bulk_push::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
}

void rai::bulk_push::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.bulk_push (*this);
}

size_t constexpr rai::node_id_handshake::query_flag;
size_t constexpr rai::node_id_handshake::response_flag;

rai::node_id_handshake::node_id_handshake (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a),
query (boost::none),
response (boost::none)
{
	error_a = deserialize (stream_a);
}

rai::node_id_handshake::node_id_handshake (boost::optional<rai::uint256_union> query, boost::optional<std::pair<rai::account, rai::signature>> response) :
message (rai::message_type::node_id_handshake),
query (query),
response (response)
{
	if (query)
	{
		header.extensions.set (query_flag);
	}
	if (response)
	{
		header.extensions.set (response_flag);
	}
}

bool rai::node_id_handshake::deserialize (rai::stream & stream_a)
{
	auto result (false);
	assert (header.type == rai::message_type::node_id_handshake);
	if (!result && header.extensions.test (query_flag))
	{
		rai::uint256_union query_hash;
		result = read (stream_a, query_hash);
		if (!result)
		{
			query = query_hash;
		}
	}
	if (!result && header.extensions.test (response_flag))
	{
		rai::account response_account;
		result = read (stream_a, response_account);
		if (!result)
		{
			rai::signature response_signature;
			result = read (stream_a, response_signature);
			if (!result)
			{
				response = std::make_pair (response_account, response_signature);
			}
		}
	}
	return result;
}

void rai::node_id_handshake::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
	if (query)
	{
		write (stream_a, *query);
	}
	if (response)
	{
		write (stream_a, response->first);
		write (stream_a, response->second);
	}
}

bool rai::node_id_handshake::operator== (rai::node_id_handshake const & other_a) const
{
	auto result (*query == *other_a.query && *response == *other_a.response);
	return result;
}

void rai::node_id_handshake::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.node_id_handshake (*this);
}

const std::string rai::musig_stage0_req::hash_prefix = "musig_stage0_req";
const std::string rai::musig_stage0_res::hash_prefix = "musig_stage0_res";
const std::string rai::musig_stage1_req::hash_prefix = "musig_stage1_req";

rai::musig_stage0_req::musig_stage0_req (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	error_a = deserialize (stream_a);
}

rai::musig_stage0_req::musig_stage0_req (std::shared_ptr<rai::state_block> block_a, rai::account rep_req_a, rai::keypair keypair_a) :
message (rai::message_type::musig_stage0_req),
block (block_a),
rep_requested (rep_req_a)
{
	header.block_type_set (block->type ());
	node_id_signature = rai::sign_message (keypair_a.prv, keypair_a.pub, hash ());
}

rai::uint256_union rai::musig_stage0_req::hash () const
{
	rai::uint256_union result;
	blake2b_state hash_l;
	auto status (blake2b_init (&hash_l, sizeof (result.bytes)));
	assert (status == 0);
	status = blake2b_update (&hash_l, hash_prefix.data (), hash_prefix.size ());
	assert (status == 0);
	auto block_hash (block->hash ());
	status = blake2b_update (&hash_l, block_hash.bytes.data (), sizeof (block_hash));
	assert (status == 0);
	status = blake2b_update (&hash_l, rep_requested.bytes.data (), sizeof (rep_requested));
	assert (status == 0);
	status = blake2b_final (&hash_l, result.bytes.data (), sizeof (result.bytes));
	assert (status == 0);
	return result;
}
bool rai::musig_stage0_req::deserialize (rai::stream & stream_a)
{
	auto result (header.block_type () != rai::block_type::state);
	if (!result)
	{
		block = std::make_shared<rai::state_block> (result, stream_a);
		if (!result)
		{
			result = rai::read (stream_a, node_id_signature);
		}
	}
	return result;
}

void rai::musig_stage0_req::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
	block->serialize (stream_a);
	rai::write (stream_a, node_id_signature);
}

bool rai::musig_stage0_req::operator== (rai::musig_stage0_req const & other_a) const
{
	auto result (*block == *other_a.block && node_id_signature == other_a.node_id_signature);
	return result;
}

void rai::musig_stage0_req::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.musig_stage0_req (*this);
}

rai::musig_stage0_res::musig_stage0_res (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	error_a = deserialize (stream_a);
}

rai::musig_stage0_res::musig_stage0_res (rai::uint256_union rb_value_a, rai::uint256_union req_id_a, rai::keypair keypair_a) :
message (rai::message_type::musig_stage0_res),
request_id (req_id_a),
rb_value (rb_value_a)
{
	rb_signature = rai::sign_message (keypair_a.prv, keypair_a.pub, hash ());
}

rai::uint256_union rai::musig_stage0_res::hash () const
{
	rai::uint256_union result;
	blake2b_state hash_l;
	auto status (blake2b_init (&hash_l, sizeof (result.bytes)));
	assert (status == 0);
	status = blake2b_update (&hash_l, hash_prefix.data (), hash_prefix.size ());
	assert (status == 0);
	status = blake2b_update (&hash_l, request_id.bytes.data (), sizeof (request_id));
	assert (status == 0);
	status = blake2b_update (&hash_l, rb_value.bytes.data (), sizeof (rb_value));
	assert (status == 0);
	status = blake2b_final (&hash_l, result.bytes.data (), sizeof (result.bytes));
	assert (status == 0);
	return result;
}

bool rai::musig_stage0_res::deserialize (rai::stream & stream_a)
{
	auto result (false);
	result = rai::read (stream_a, rb_value);
	if (!result)
	{
		result = rai::read (stream_a, rb_signature);
		if (!result)
		{
			result = rai::read (stream_a, request_id);
		}
	}
	return result;
}

void rai::musig_stage0_res::serialize (rai::stream & stream_a)
{
	rai::write (stream_a, rb_value);
	rai::write (stream_a, rb_signature);
}

bool rai::musig_stage0_res::operator== (rai::musig_stage0_res const & other_a) const
{
	auto result (rb_value == other_a.rb_value && rb_signature == other_a.rb_signature);
	return result;
}

void rai::musig_stage0_res::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.musig_stage0_res (*this);
}

rai::musig_stage1_req::musig_stage1_req (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	error_a = deserialize (stream_a);
}

rai::musig_stage1_req::musig_stage1_req (rai::uint256_union rb_total_a, rai::uint256_union req_id_a, rai::public_key agg_pubkey_a, rai::keypair keypair_a) :
message (rai::message_type::musig_stage1_req),
rb_total (rb_total_a),
request_id (req_id_a),
agg_pubkey (agg_pubkey_a)
{
	node_id_signature = rai::sign_message (keypair_a.prv, keypair_a.pub, hash ());
}

rai::uint256_union rai::musig_stage1_req::hash () const
{
	rai::uint256_union result;
	blake2b_state hash_l;
	auto status (blake2b_init (&hash_l, sizeof (result.bytes)));
	assert (status == 0);
	status = blake2b_update (&hash_l, hash_prefix.data (), hash_prefix.size ());
	assert (status == 0);
	status = blake2b_update (&hash_l, request_id.bytes.data (), sizeof (request_id));
	assert (status == 0);
	status = blake2b_update (&hash_l, rb_total.bytes.data (), sizeof (rb_total));
	assert (status == 0);
	status = blake2b_update (&hash_l, agg_pubkey.bytes.data (), sizeof (agg_pubkey));
	assert (status == 0);
	status = blake2b_final (&hash_l, result.bytes.data (), sizeof (result.bytes));
	assert (status == 0);
	return result;
}

bool rai::musig_stage1_req::deserialize (rai::stream & stream_a)
{
	auto result (false);
	result = rai::read (stream_a, rb_total);
	if (!result)
	{
		result = rai::read (stream_a, agg_pubkey);
		if (!result)
		{
			result = rai::read (stream_a, node_id_signature);
		}
	}
	return result;
}

void rai::musig_stage1_req::serialize (rai::stream & stream_a)
{
	header.serialize (stream_a);
	rai::write (stream_a, rb_total);
	rai::write (stream_a, agg_pubkey);
	rai::write (stream_a, node_id_signature);
}

bool rai::musig_stage1_req::operator== (rai::musig_stage1_req const & other_a) const
{
	auto result (rb_total == other_a.rb_total && agg_pubkey == other_a.agg_pubkey && node_id_signature == other_a.node_id_signature);
	return result;
}

void rai::musig_stage1_req::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.musig_stage1_req (*this);
}

rai::musig_stage1_res::musig_stage1_res (bool & error_a, rai::stream & stream_a, rai::message_header const & header_a) :
message (header_a)
{
	error_a = deserialize (stream_a);
}

rai::musig_stage1_res::musig_stage1_res (rai::uint256_union s_value_a) :
message (rai::message_type::musig_stage1_res),
s_value (s_value_a)
{
}

bool rai::musig_stage1_res::deserialize (rai::stream & stream_a)
{
	return rai::read (stream_a, s_value);
}

void rai::musig_stage1_res::serialize (rai::stream & stream_a)
{
	rai::write (stream_a, s_value);
}

bool rai::musig_stage1_res::operator== (rai::musig_stage1_res const & other_a) const
{
	return s_value == other_a.s_value;
}

void rai::musig_stage1_res::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.musig_stage1_res (*this);
}

rai::message_visitor::~message_visitor ()
{
}
