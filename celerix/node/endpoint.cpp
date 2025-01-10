#include <celerix/lib/blocks.hpp>
#include <celerix/lib/memory.hpp>
#include <celerix/lib/stream.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/endpoint.hpp>
#include <celerix/node/network.hpp>
#include <celerix/node/wallet.hpp>
#include <celerix/secure/vote.hpp>

#include <boost/format.hpp>

uint64_t celerix::ip_address_hash_raw (boost::asio::ip::address const & ip_a, uint16_t port)
{
	debug_assert (ip_a.is_v6 ());
	uint64_t result;
	celerix::uint128_union address;
	address.bytes = ip_a.to_v6 ().to_bytes ();
	blake2b_state state;
	blake2b_init (&state, sizeof (result));
	blake2b_update (&state, celerix::hardened_constants::get ().random_128.bytes.data (), celerix::hardened_constants::get ().random_128.bytes.size ());
	if (port != 0)
	{
		blake2b_update (&state, &port, sizeof (port));
	}
	blake2b_update (&state, address.bytes.data (), address.bytes.size ());
	blake2b_final (&state, &result, sizeof (result));
	return result;
}

uint64_t celerix::endpoint_hash_raw (celerix::endpoint const & endpoint_a)
{
	uint64_t result (celerix::ip_address_hash_raw (endpoint_a.address (), endpoint_a.port ()));
	return result;
}

bool celerix::parse_port (std::string const & string_a, uint16_t & port_a)
{
	bool result = false;
	try
	{
		port_a = boost::lexical_cast<uint16_t> (string_a);
	}
	catch (...)
	{
		result = true;
	}
	return result;
}

// Can handle both ipv4 & ipv6 addresses (with and without square brackets)
bool celerix::parse_address (std::string const & address_text_a, boost::asio::ip::address & address_a)
{
	auto address_text = address_text_a;
	if (!address_text.empty () && address_text.front () == '[' && address_text.back () == ']')
	{
		// Chop the square brackets off as make_address doesn't always like them
		address_text = address_text.substr (1, address_text.size () - 2);
	}

	boost::system::error_code address_ec;
	address_a = boost::asio::ip::make_address (address_text, address_ec);
	return !!address_ec;
}

bool celerix::parse_address_port (std::string const & string, boost::asio::ip::address & address_a, uint16_t & port_a)
{
	auto result (false);
	auto port_position (string.rfind (':'));
	if (port_position != std::string::npos && port_position > 0)
	{
		std::string port_string (string.substr (port_position + 1));
		try
		{
			uint16_t port;
			result = parse_port (port_string, port);
			if (!result)
			{
				boost::system::error_code ec;
				auto address (boost::asio::ip::make_address_v6 (string.substr (0, port_position), ec));
				if (!ec)
				{
					address_a = address;
					port_a = port;
				}
				else
				{
					result = true;
				}
			}
			else
			{
				result = true;
			}
		}
		catch (...)
		{
			result = true;
		}
	}
	else
	{
		result = true;
	}
	return result;
}

bool celerix::parse_endpoint (std::string const & string, celerix::endpoint & endpoint_a)
{
	boost::asio::ip::address address;
	uint16_t port;
	auto result (parse_address_port (string, address, port));
	if (!result)
	{
		endpoint_a = celerix::endpoint (address, port);
	}
	return result;
}

std::optional<celerix::endpoint> celerix::parse_endpoint (const std::string & str)
{
	celerix::endpoint endpoint;
	if (!parse_endpoint (str, endpoint))
	{
		return endpoint; // Success
	}
	return {};
}

bool celerix::parse_tcp_endpoint (std::string const & string, celerix::tcp_endpoint & endpoint_a)
{
	boost::asio::ip::address address;
	uint16_t port;
	auto result (parse_address_port (string, address, port));
	if (!result)
	{
		endpoint_a = celerix::tcp_endpoint (address, port);
	}
	return result;
}

celerix::node_singleton_memory_pool_purge_guard::node_singleton_memory_pool_purge_guard () :
	cleanup_guard ({ celerix::block_memory_pool_purge, celerix::purge_shared_ptr_singleton_pool_memory<celerix::vote>, celerix::purge_shared_ptr_singleton_pool_memory<celerix::election> })
{
}
