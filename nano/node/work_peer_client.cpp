#include <nano/boost/beast/core.hpp>
#include <nano/boost/beast/http.hpp>
#include <nano/lib/assert.hpp>
#include <nano/lib/async.hpp>
#include <nano/lib/constants.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/node/work_peer_client.hpp>

#include <boost/asio.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <memory>
#include <sstream>
#include <vector>

namespace beast = boost::beast;
namespace http = boost::beast::http;

namespace
{
using time_point = std::chrono::steady_clock::time_point;

// A name lookup together with the timer that bounds it
struct lookup
{
	explicit lookup (asio::any_io_executor const & executor) :
		resolver{ executor },
		timer{ executor }
	{
	}

	asio::ip::tcp::resolver resolver;
	asio::steady_timer timer;
	bool done{ false }; // Set by whichever of the lookup and the wait finishes first
	boost::system::error_code ec;
	asio::ip::tcp::resolver::results_type results;
};

struct resolved
{
	nano::work_peer_status status;
	std::vector<asio::ip::tcp::endpoint> endpoints;
};

// One HTTP exchange as far as it got
struct exchanged
{
	nano::work_peer_status status;
	bool request_sent{ false };
	http::response<http::string_body> response;
};

// How an operation that did not succeed ended, `otherwise` being the status of a plain failure
nano::work_peer_status to_status (boost::system::error_code const & ec, nano::work_peer_status otherwise)
{
	if (ec == beast::error::timeout)
	{
		return nano::work_peer_status::timeout;
	}

	if (ec == asio::error::operation_aborted)
	{
		return nano::work_peer_status::cancelled;
	}

	return otherwise;
}

// Resolves the peer's address, giving up at the deadline even when the system resolver has not answered yet
asio::awaitable<resolved> resolve (nano::work_peer peer, time_point deadline)
{
	// An IP address needs no lookup
	boost::system::error_code parse_ec;
	auto address = asio::ip::make_address (peer.address, parse_ec);
	if (!parse_ec)
	{
		// A mapped address is connected to as the IPv4 address it stands for, which works without a dual stack socket
		if (address.is_v6 () && address.to_v6 ().is_v4_mapped ())
		{
			address = asio::ip::make_address_v4 (asio::ip::v4_mapped, address.to_v6 ());
		}

		co_return resolved{ nano::work_peer_status::success, { asio::ip::tcp::endpoint{ address, peer.port } } };
	}

	// The lookup cannot be interrupted, so wait on a timer that the lookup cuts short, and leave the lookup behind if the timer wins
	auto state = std::make_shared<lookup> (co_await asio::this_coro::executor);
	state->timer.expires_at (deadline);
	state->resolver.async_resolve (peer.address, std::to_string (peer.port), [state] (boost::system::error_code const & ec, asio::ip::tcp::resolver::results_type results) {
		if (!state->done)
		{
			state->done = true;
			state->ec = ec;
			state->results = std::move (results);
			state->timer.cancel ();
		}
	});

	auto const [wait_ec] = co_await state->timer.async_wait (asio::as_tuple (asio::use_awaitable));

	if (!state->done)
	{
		state->done = true;
		state->resolver.cancel ();

		// The wait ran out on its own, or the coroutine was cancelled
		auto const status = wait_ec ? nano::work_peer_status::cancelled : nano::work_peer_status::timeout;
		co_return resolved{ status, {} };
	}

	if (state->ec)
	{
		co_return resolved{ nano::work_peer_status::unreachable, {} };
	}

	resolved result{ nano::work_peer_status::success, {} };
	for (auto const & entry : state->results)
	{
		result.endpoints.push_back (entry.endpoint ());
	}

	co_return result;
}

// The value of the Host header for the peer
std::string host_of (nano::work_peer const & peer)
{
	boost::system::error_code ec;
	auto const address = asio::ip::make_address (peer.address, ec);
	if (!ec && address.is_v6 () && address.to_v6 ().is_v4_mapped ())
	{
		return asio::ip::make_address_v4 (asio::ip::v4_mapped, address.to_v6 ()).to_string ();
	}

	return peer.address;
}

std::string to_json (boost::property_tree::ptree const & tree)
{
	std::stringstream stream;
	boost::property_tree::write_json (stream, tree);
	return stream.str ();
}

// Posts the body to the peer over a new connection and reads its answer, each step ending at its deadline
asio::awaitable<exchanged> exchange (nano::work_peer peer, std::string body, time_point connect_deadline, time_point deadline)
{
	exchanged result{ nano::work_peer_status::io_error };

	// A cancellation only ends the operation that is pending when it arrives, one that came in between is checked for before each step
	if (co_await nano::async::cancelled ())
	{
		result.status = nano::work_peer_status::cancelled;
		co_return result;
	}

	// Resolve
	auto const resolved = co_await resolve (peer, connect_deadline);
	if (resolved.status != nano::work_peer_status::success)
	{
		result.status = resolved.status;
		co_return result;
	}

	// Connect to the first endpoint that takes the connection
	// Each one gets an equal part of the time that is left, so that an endpoint that does not answer leaves the next one its time
	beast::tcp_stream stream{ co_await asio::this_coro::executor };
	boost::system::error_code connect_ec{ asio::error::host_unreachable };
	for (size_t position = 0; position < resolved.endpoints.size (); ++position)
	{
		if (co_await nano::async::cancelled ())
		{
			result.status = nano::work_peer_status::cancelled;
			co_return result;
		}

		auto const now = std::chrono::steady_clock::now ();
		auto const left = resolved.endpoints.size () - position;
		debug_assert (left > 0);
		auto const part = (connect_deadline - now) / left;

		// A stream that failed to connect has to be closed before it can connect elsewhere
		stream.close ();
		stream.expires_at (now + part);
		auto const [ec] = co_await stream.async_connect (resolved.endpoints[position], asio::as_tuple (asio::use_awaitable));
		connect_ec = ec;

		// A cancellation ends the exchange, any other failure moves on to the next endpoint
		if (!ec || ec == asio::error::operation_aborted)
		{
			break;
		}
	}

	if (connect_ec)
	{
		result.status = to_status (connect_ec, nano::work_peer_status::unreachable);
		co_return result;
	}

	if (co_await nano::async::cancelled ())
	{
		result.status = nano::work_peer_status::cancelled;
		co_return result;
	}

	// Send the request
	http::request<http::string_body> request{ http::verb::post, "/", /* HTTP version */ 11 };
	request.set (http::field::host, host_of (peer));
	request.set (http::field::content_type, "application/json");
	request.body () = std::move (body);
	request.prepare_payload ();

	stream.expires_at (deadline);
	result.request_sent = true;
	auto const [write_ec, written] = co_await http::async_write (stream, request, asio::as_tuple (asio::use_awaitable));
	if (write_ec)
	{
		result.status = to_status (write_ec, nano::work_peer_status::io_error);
		co_return result;
	}

	if (co_await nano::async::cancelled ())
	{
		result.status = nano::work_peer_status::cancelled;
		co_return result;
	}

	// Read the answer
	beast::flat_buffer buffer;
	auto const [read_ec, read] = co_await http::async_read (stream, buffer, result.response, asio::as_tuple (asio::use_awaitable));
	if (read_ec)
	{
		result.status = to_status (read_ec, nano::work_peer_status::io_error);
		co_return result;
	}

	result.status = nano::work_peer_status::success;
	co_return result;
}
}

nano::work_peer_client::work_peer_client (nano::work_thresholds const & thresholds_a, timeouts const & config_a, nano::logger & logger_a) :
	thresholds{ thresholds_a },
	logger{ logger_a },
	config{ config_a }
{
}

asio::awaitable<nano::work_peer_response> nano::work_peer_client::generate (nano::work_peer peer, nano::work_request request, std::chrono::steady_clock::time_point deadline) const
{
	// A cancelled exchange ends through the error of its pending operation, not through an exception
	auto const throwing = co_await asio::this_coro::throw_if_cancelled ();
	co_await asio::this_coro::throw_if_cancelled (false);

	nano::work_peer_response result;
	try
	{
		boost::property_tree::ptree tree;
		tree.put ("action", "work_generate");
		tree.put ("hash", request.root.to_string ());
		tree.put ("difficulty", nano::to_string_hex (request.difficulty));
		if (request.account)
		{
			tree.put ("account", request.account->to_account ());
		}

		auto const connect_deadline = std::min (deadline, std::chrono::steady_clock::now () + config.connect);
		auto const exchanged = co_await exchange (peer, to_json (tree), connect_deadline, deadline);

		result.status = exchanged.status;
		result.request_sent = exchanged.request_sent;

		if (exchanged.status == nano::work_peer_status::success)
		{
			result = parse (exchanged.response.result_int (), exchanged.response.body (), request);
			result.request_sent = true;
		}
	}
	catch (std::exception const & ex)
	{
		logger.error (nano::log::type::work_peer_client, "Error asking work peer {} for work: {}", peer.to_string (), ex.what ());
		result.status = nano::work_peer_status::io_error;
	}

	if (result.status != nano::work_peer_status::success && result.status != nano::work_peer_status::cancelled)
	{
		logger.debug (nano::log::type::work_peer_client, "Work peer {} did not deliver work for root {}: {}", peer.to_string (), request.root.to_string (), to_string (result.status));
	}

	co_await asio::this_coro::throw_if_cancelled (throwing);
	co_return result;
}

asio::awaitable<void> nano::work_peer_client::cancel (nano::work_peer peer, nano::root root) const
{
	auto const throwing = co_await asio::this_coro::throw_if_cancelled ();
	co_await asio::this_coro::throw_if_cancelled (false);

	try
	{
		boost::property_tree::ptree tree;
		tree.put ("action", "work_cancel");
		tree.put ("hash", root.to_string ());

		// The answer says nothing this node acts on, it is read so that the peer sees an orderly exchange
		auto const deadline = std::chrono::steady_clock::now () + config.cancel;
		auto const exchanged = co_await exchange (peer, to_json (tree), deadline, deadline);

		if (exchanged.status != nano::work_peer_status::success && exchanged.status != nano::work_peer_status::cancelled)
		{
			logger.debug (nano::log::type::work_peer_client, "Unable to send work cancel to work peer {}: {}", peer.to_string (), to_string (exchanged.status));
		}
	}
	catch (std::exception const & ex)
	{
		logger.error (nano::log::type::work_peer_client, "Error sending work cancel to work peer {}: {}", peer.to_string (), ex.what ());
	}

	co_await asio::this_coro::throw_if_cancelled (throwing);
}

nano::work_peer_response nano::work_peer_client::parse (unsigned http_status, std::string const & body, nano::work_request const & request) const
{
	nano::work_peer_response result;

	if (http_status != static_cast<unsigned> (http::status::ok))
	{
		result.status = nano::work_peer_status::http_error;
		return result;
	}

	boost::property_tree::ptree tree;
	try
	{
		std::stringstream stream{ body };
		boost::property_tree::read_json (stream, tree);
	}
	catch (std::exception const &)
	{
		result.status = nano::work_peer_status::malformed;
		return result;
	}

	if (tree.count ("error") != 0)
	{
		result.status = nano::work_peer_status::refused;
		return result;
	}

	auto const work_text = tree.get_optional<std::string> ("work");
	uint64_t work{ 0 };
	if (!work_text || nano::from_string_hex (*work_text, work))
	{
		result.status = nano::work_peer_status::malformed;
		return result;
	}

	if (thresholds.difficulty (request.version, request.root, work) < request.difficulty)
	{
		result.status = nano::work_peer_status::insufficient;
		return result;
	}

	result.status = nano::work_peer_status::success;
	result.work = work;
	return result;
}

std::string_view nano::to_string (nano::work_peer_status status)
{
	return nano::enum_to_string (status);
}

nano::stat::detail nano::to_stat_detail (nano::work_peer_status status)
{
	return nano::enum_convert<nano::stat::detail> (status);
}
