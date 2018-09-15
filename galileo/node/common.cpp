
#include <galileo/node/common.hpp>

#include <galileo/lib/work.hpp>
#include <galileo/node/wallet.hpp>

std::array<uint8_t, 2> constexpr galileo::message_header::magic_number;
size_t constexpr galileo::message_header::ipv4_only_position;
size_t constexpr galileo::message_header::bootstrap_server_position;
std::bitset<16> constexpr galileo::message_header::block_type_mask;

galileo::message_header::message_header (galileo::message_type type_a) :
version_max (galileo::protocol_version),
version_using (galileo::protocol_version),
version_min (galileo::protocol_version_min),
type (type_a)
{
}

galileo::message_header::message_header (bool & error_a, galileo::stream & stream_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void galileo::message_header::serialize (galileo::stream & stream_a)
{
	galileo::write (stream_a, galileo::message_header::magic_number);
	galileo::write (stream_a, version_max);
	galileo::write (stream_a, version_using);
	galileo::write (stream_a, version_min);
	galileo::write (stream_a, type);
	galileo::write (stream_a, static_cast<uint16_t> (extensions.to_ullong ()));
}

bool galileo::message_header::deserialize (galileo::stream & stream_a)
{
	uint16_t extensions_l;
	std::array<uint8_t, 2> magic_number_l;
	auto result (galileo::read (stream_a, magic_number_l));
	result = result || magic_number_l != magic_number;
	result = result || galileo::read (stream_a, version_max);
	result = result || galileo::read (stream_a, version_using);
	result = result || galileo::read (stream_a, version_min);
	result = result || galileo::read (stream_a, type);
	result = result || galileo::read (stream_a, extensions_l);
	if (!result)
	{
		extensions = extensions_l;
	}
	return result;
}

galileo::message::message (galileo::message_type type_a) :
header (type_a)
{
}

galileo::message::message (galileo::message_header const & header_a) :
header (header_a)
{
}

galileo::block_type galileo::message_header::block_type () const
{
	return static_cast<galileo::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void galileo::message_header::block_type_set (galileo::block_type type_a)
{
	extensions &= ~block_type_mask;
	extensions |= std::bitset<16> (static_cast<unsigned long long> (type_a) << 8);
}

bool galileo::message_header::ipv4_only ()
{
	return extensions.test (ipv4_only_position);
}

void galileo::message_header::ipv4_only_set (bool value_a)
{
	extensions.set (ipv4_only_position, value_a);
}

// MTU - IP header - UDP header
const size_t galileo::message_parser::max_safe_udp_message_size = 508;

galileo::message_parser::message_parser (galileo::message_visitor & visitor_a, galileo::work_pool & pool_a) :
visitor (visitor_a),
pool (pool_a),
status (parse_status::success)
{
}

void galileo::message_parser::deserialize_buffer (uint8_t const * buffer_a, size_t size_a)
{
	status = parse_status::success;
	auto error (false);
	if (size_a <= max_safe_udp_message_size)
	{
		// Guaranteed to be deliverable
		galileo::bufferstream stream (buffer_a, size_a);
		galileo::message_header header (error, stream);
		if (!error)
		{
			if (galileo::galileo_network == galileo::galileo_networks::galileo_beta_network && header.version_using < galileo::protocol_version)
			{
				status = parse_status::outdated_version;
			}
			else
			{
				switch (header.type)
				{
					case galileo::message_type::keepalive:
					{
						deserialize_keepalive (stream, header);
						break;
					}
					case galileo::message_type::publish:
					{
						deserialize_publish (stream, header);
						break;
					}
					case galileo::message_type::confirm_req:
					{
						deserialize_confirm_req (stream, header);
						break;
					}
					case galileo::message_type::confirm_ack:
					{
						deserialize_confirm_ack (stream, header);
						break;
					}
					case galileo::message_type::node_id_handshake:
					{
						deserialize_node_id_handshake (stream, header);
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

void galileo::message_parser::deserialize_keepalive (galileo::stream & stream_a, galileo::message_header const & header_a)
{
	auto error (false);
	galileo::keepalive incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		visitor.keepalive (incoming);
	}
	else
	{
		status = parse_status::invalid_keepalive_message;
	}
}

void galileo::message_parser::deserialize_publish (galileo::stream & stream_a, galileo::message_header const & header_a)
{
	auto error (false);
	galileo::publish incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		if (!galileo::work_validate (*incoming.block))
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

void galileo::message_parser::deserialize_confirm_req (galileo::stream & stream_a, galileo::message_header const & header_a)
{
	auto error (false);
	galileo::confirm_req incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		if (!galileo::work_validate (*incoming.block))
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

void galileo::message_parser::deserialize_confirm_ack (galileo::stream & stream_a, galileo::message_header const & header_a)
{
	auto error (false);
	galileo::confirm_ack incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		for (auto & vote_block : incoming.vote->blocks)
		{
			if (!vote_block.which ())
			{
				auto block (boost::get<std::shared_ptr<galileo::block>> (vote_block));
				if (galileo::work_validate (*block))
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

void galileo::message_parser::deserialize_node_id_handshake (galileo::stream & stream_a, galileo::message_header const & header_a)
{
	bool error_l (false);
	galileo::node_id_handshake incoming (error_l, stream_a, header_a);
	if (!error_l && at_end (stream_a))
	{
		visitor.node_id_handshake (incoming);
	}
	else
	{
		status = parse_status::invalid_node_id_handshake_message;
	}
}

bool galileo::message_parser::at_end (galileo::stream & stream_a)
{
	uint8_t junk;
	auto end (galileo::read (stream_a, junk));
	return end;
}

galileo::keepalive::keepalive () :
message (galileo::message_type::keepalive)
{
	galileo::endpoint endpoint (boost::asio::ip::address_v6{}, 0);
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
	{
		*i = endpoint;
	}
}

galileo::keepalive::keepalive (bool & error_a, galileo::stream & stream_a, galileo::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void galileo::keepalive::visit (galileo::message_visitor & visitor_a) const
{
	visitor_a.keepalive (*this);
}

void galileo::keepalive::serialize (galileo::stream & stream_a)
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

bool galileo::keepalive::deserialize (galileo::stream & stream_a)
{
	assert (header.type == galileo::message_type::keepalive);
	auto error (false);
	for (auto i (peers.begin ()), j (peers.end ()); i != j && !error; ++i)
	{
		std::array<uint8_t, 16> address;
		uint16_t port;
		if (!read (stream_a, address) && !read (stream_a, port))
		{
			*i = galileo::endpoint (boost::asio::ip::address_v6 (address), port);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool galileo::keepalive::operator== (galileo::keepalive const & other_a) const
{
	return peers == other_a.peers;
}

galileo::publish::publish (bool & error_a, galileo::stream & stream_a, galileo::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

galileo::publish::publish (std::shared_ptr<galileo::block> block_a) :
message (galileo::message_type::publish),
block (block_a)
{
	header.block_type_set (block->type ());
}

bool galileo::publish::deserialize (galileo::stream & stream_a)
{
	assert (header.type == galileo::message_type::publish);
	block = galileo::deserialize_block (stream_a, header.block_type ());
	auto result (block == nullptr);
	return result;
}

void galileo::publish::serialize (galileo::stream & stream_a)
{
	assert (block != nullptr);
	header.serialize (stream_a);
	block->serialize (stream_a);
}

void galileo::publish::visit (galileo::message_visitor & visitor_a) const
{
	visitor_a.publish (*this);
}

bool galileo::publish::operator== (galileo::publish const & other_a) const
{
	return *block == *other_a.block;
}

galileo::confirm_req::confirm_req (bool & error_a, galileo::stream & stream_a, galileo::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

galileo::confirm_req::confirm_req (std::shared_ptr<galileo::block> block_a) :
message (galileo::message_type::confirm_req),
block (block_a)
{
	header.block_type_set (block->type ());
}

bool galileo::confirm_req::deserialize (galileo::stream & stream_a)
{
	assert (header.type == galileo::message_type::confirm_req);
	block = galileo::deserialize_block (stream_a, header.block_type ());
	auto result (block == nullptr);
	return result;
}

void galileo::confirm_req::visit (galileo::message_visitor & visitor_a) const
{
	visitor_a.confirm_req (*this);
}

void galileo::confirm_req::serialize (galileo::stream & stream_a)
{
	assert (block != nullptr);
	header.serialize (stream_a);
	block->serialize (stream_a);
}

bool galileo::confirm_req::operator== (galileo::confirm_req const & other_a) const
{
	return *block == *other_a.block;
}

galileo::confirm_ack::confirm_ack (bool & error_a, galileo::stream & stream_a, galileo::message_header const & header_a) :
message (header_a),
vote (std::make_shared<galileo::vote> (error_a, stream_a, header.block_type ()))
{
}

galileo::confirm_ack::confirm_ack (std::shared_ptr<galileo::vote> vote_a) :
message (galileo::message_type::confirm_ack),
vote (vote_a)
{
	auto & first_vote_block (vote_a->blocks[0]);
	if (first_vote_block.which ())
	{
		header.block_type_set (galileo::block_type::not_a_block);
	}
	else
	{
		header.block_type_set (boost::get<std::shared_ptr<galileo::block>> (first_vote_block)->type ());
	}
}

bool galileo::confirm_ack::deserialize (galileo::stream & stream_a)
{
	assert (header.type == galileo::message_type::confirm_ack);
	auto result (vote->deserialize (stream_a));
	return result;
}

void galileo::confirm_ack::serialize (galileo::stream & stream_a)
{
	assert (header.block_type () == galileo::block_type::not_a_block || header.block_type () == galileo::block_type::send || header.block_type () == galileo::block_type::receive || header.block_type () == galileo::block_type::open || header.block_type () == galileo::block_type::change || header.block_type () == galileo::block_type::state);
	header.serialize (stream_a);
	vote->serialize (stream_a, header.block_type ());
}

bool galileo::confirm_ack::operator== (galileo::confirm_ack const & other_a) const
{
	auto result (*vote == *other_a.vote);
	return result;
}

void galileo::confirm_ack::visit (galileo::message_visitor & visitor_a) const
{
	visitor_a.confirm_ack (*this);
}

galileo::frontier_req::frontier_req () :
message (galileo::message_type::frontier_req)
{
}

galileo::frontier_req::frontier_req (bool & error_a, galileo::stream & stream_a, galileo::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

bool galileo::frontier_req::deserialize (galileo::stream & stream_a)
{
	assert (header.type == galileo::message_type::frontier_req);
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

void galileo::frontier_req::serialize (galileo::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, start.bytes);
	write (stream_a, age);
	write (stream_a, count);
}

void galileo::frontier_req::visit (galileo::message_visitor & visitor_a) const
{
	visitor_a.frontier_req (*this);
}

bool galileo::frontier_req::operator== (galileo::frontier_req const & other_a) const
{
	return start == other_a.start && age == other_a.age && count == other_a.count;
}

galileo::bulk_pull::bulk_pull () :
message (galileo::message_type::bulk_pull)
{
}

galileo::bulk_pull::bulk_pull (bool & error_a, galileo::stream & stream_a, galileo::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void galileo::bulk_pull::visit (galileo::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull (*this);
}

bool galileo::bulk_pull::deserialize (galileo::stream & stream_a)
{
	assert (header.type == galileo::message_type::bulk_pull);
	auto result (read (stream_a, start));
	if (!result)
	{
		result = read (stream_a, end);
	}
	return result;
}

void galileo::bulk_pull::serialize (galileo::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, start);
	write (stream_a, end);
}

galileo::bulk_pull_account::bulk_pull_account () :
message (galileo::message_type::bulk_pull_account)
{
}

galileo::bulk_pull_account::bulk_pull_account (bool & error_a, galileo::stream & stream_a, galileo::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void galileo::bulk_pull_account::visit (galileo::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull_account (*this);
}

bool galileo::bulk_pull_account::deserialize (galileo::stream & stream_a)
{
	assert (header.type == galileo::message_type::bulk_pull_account);
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

void galileo::bulk_pull_account::serialize (galileo::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, account);
	write (stream_a, minimum_amount);
	write (stream_a, flags);
}

galileo::bulk_pull_blocks::bulk_pull_blocks () :
message (galileo::message_type::bulk_pull_blocks)
{
}

galileo::bulk_pull_blocks::bulk_pull_blocks (bool & error_a, galileo::stream & stream_a, galileo::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void galileo::bulk_pull_blocks::visit (galileo::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull_blocks (*this);
}

bool galileo::bulk_pull_blocks::deserialize (galileo::stream & stream_a)
{
	assert (header.type == galileo::message_type::bulk_pull_blocks);
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

void galileo::bulk_pull_blocks::serialize (galileo::stream & stream_a)
{
	header.serialize (stream_a);
	write (stream_a, min_hash);
	write (stream_a, max_hash);
	write (stream_a, mode);
	write (stream_a, max_count);
}

galileo::bulk_push::bulk_push () :
message (galileo::message_type::bulk_push)
{
}

galileo::bulk_push::bulk_push (galileo::message_header const & header_a) :
message (header_a)
{
}

bool galileo::bulk_push::deserialize (galileo::stream & stream_a)
{
	assert (header.type == galileo::message_type::bulk_push);
	return false;
}

void galileo::bulk_push::serialize (galileo::stream & stream_a)
{
	header.serialize (stream_a);
}

void galileo::bulk_push::visit (galileo::message_visitor & visitor_a) const
{
	visitor_a.bulk_push (*this);
}

size_t constexpr galileo::node_id_handshake::query_flag;
size_t constexpr galileo::node_id_handshake::response_flag;

galileo::node_id_handshake::node_id_handshake (bool & error_a, galileo::stream & stream_a, galileo::message_header const & header_a) :
message (header_a),
query (boost::none),
response (boost::none)
{
	error_a = deserialize (stream_a);
}

galileo::node_id_handshake::node_id_handshake (boost::optional<galileo::uint256_union> query, boost::optional<std::pair<galileo::account, galileo::signature>> response) :
message (galileo::message_type::node_id_handshake),
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

bool galileo::node_id_handshake::deserialize (galileo::stream & stream_a)
{
	auto result (false);
	assert (header.type == galileo::message_type::node_id_handshake);
	if (!result && header.extensions.test (query_flag))
	{
		galileo::uint256_union query_hash;
		result = read (stream_a, query_hash);
		if (!result)
		{
			query = query_hash;
		}
	}
	if (!result && header.extensions.test (response_flag))
	{
		galileo::account response_account;
		result = read (stream_a, response_account);
		if (!result)
		{
			galileo::signature response_signature;
			result = read (stream_a, response_signature);
			if (!result)
			{
				response = std::make_pair (response_account, response_signature);
			}
		}
	}
	return result;
}

void galileo::node_id_handshake::serialize (galileo::stream & stream_a)
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

bool galileo::node_id_handshake::operator== (galileo::node_id_handshake const & other_a) const
{
	auto result (*query == *other_a.query && *response == *other_a.response);
	return result;
}

void galileo::node_id_handshake::visit (galileo::message_visitor & visitor_a) const
{
	visitor_a.node_id_handshake (*this);
}

galileo::message_visitor::~message_visitor ()
{
}
