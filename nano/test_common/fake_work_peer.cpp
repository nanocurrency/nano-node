#include <nano/boost/beast/core.hpp>
#include <nano/boost/beast/http.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/test_common/fake_work_peer.hpp>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <sstream>

namespace beast = boost::beast;
namespace http = boost::beast::http;

namespace
{
std::string to_json (boost::property_tree::ptree const & tree)
{
	std::stringstream stream;
	boost::property_tree::write_json (stream, tree);
	return stream.str ();
}

// A work value that stays below the difficulty for the root
uint64_t insufficient_work (nano::root const & root, uint64_t difficulty)
{
	uint64_t work{ 0 };
	while (nano::dev::network_params.work.difficulty (nano::work_version::work_1, root, work) >= difficulty)
	{
		++work;
	}

	return work;
}
}

nano::test::fake_work_peer::fake_work_peer (asio::io_context & io_ctx, fake_work_peer_mode mode_a, std::chrono::milliseconds delay_a) :
	delay{ delay_a },
	pool{ nano::dev::network_params.network, /* max threads */ 1 },
	strand{ io_ctx.get_executor () },
	acceptor{ strand, asio::ip::tcp::endpoint{ asio::ip::address_v4::loopback (), /* any port */ 0 } },
	local_port{ acceptor.local_endpoint ().port () },
	mode{ mode_a },
	task{ strand }
{
	// The first pool thread of a process takes a while to come up under a sanitizer, which no request to the peer should wait for
	pool.generate (nano::work_version::work_1, nano::root{ 1 }, nano::dev::network_params.work.base);

	task = nano::async::task (strand, run ());
}

nano::test::fake_work_peer::~fake_work_peer ()
{
	stop ();
}

void nano::test::fake_work_peer::stop ()
{
	if (stopped.exchange (true))
	{
		return;
	}

	// End the accept loop first, nothing adds connections after it
	task.cancel ();
	task.join ();

	for (auto & connection : connections)
	{
		connection.cancel ();
	}

	for (auto & connection : connections)
	{
		connection.join ();
	}
}

void nano::test::fake_work_peer::set_mode (fake_work_peer_mode mode_a)
{
	mode = mode_a;
}

nano::work_peer nano::test::fake_work_peer::peer () const
{
	return nano::work_peer{ "127.0.0.1", local_port };
}

uint16_t nano::test::fake_work_peer::port () const
{
	return local_port;
}

size_t nano::test::fake_work_peer::generate_count () const
{
	return generates;
}

size_t nano::test::fake_work_peer::cancel_count () const
{
	return cancels;
}

size_t nano::test::fake_work_peer::account_count () const
{
	return accounts;
}

asio::awaitable<void> nano::test::fake_work_peer::run ()
{
	// An error ends the loop, the usual one being the cancellation by `stop`
	while (true)
	{
		auto [ec, socket] = co_await acceptor.async_accept (asio::as_tuple (asio::use_awaitable));
		if (ec)
		{
			co_return;
		}

		connections.emplace_back (strand, serve (std::move (socket)));
	}
}

asio::awaitable<void> nano::test::fake_work_peer::serve (asio::ip::tcp::socket socket)
{
	// Read the request, a connection that breaks or is cancelled ends here
	beast::flat_buffer buffer;
	http::request<http::string_body> request;
	auto const [read_ec, read] = co_await http::async_read (socket, buffer, request, asio::as_tuple (asio::use_awaitable));
	if (read_ec)
	{
		co_return;
	}

	boost::property_tree::ptree tree;
	std::stringstream stream{ request.body () };
	boost::property_tree::read_json (stream, tree);

	auto const action = tree.get<std::string> ("action");
	nano::root root{ 0 };
	root.decode_hex (tree.get<std::string> ("hash"));

	http::response<http::string_body> response{ http::status::ok, request.version () };
	response.set (http::field::content_type, "application/json");
	response.keep_alive (false);

	if (action == "work_cancel")
	{
		++cancels;

		boost::property_tree::ptree answer;
		answer.put ("success", "");
		response.body () = to_json (answer);
	}
	else
	{
		++generates;

		if (tree.count ("account") != 0)
		{
			++accounts;
		}

		uint64_t difficulty{ 0 };
		nano::from_string_hex (tree.get<std::string> ("difficulty"), difficulty);

		switch (mode.load ())
		{
			case fake_work_peer_mode::slow:
			case fake_work_peer_mode::good:
			{
				if (mode == fake_work_peer_mode::slow)
				{
					asio::steady_timer timer{ strand };
					timer.expires_after (delay);
					auto const [wait_ec] = co_await timer.async_wait (asio::as_tuple (asio::use_awaitable));
					if (wait_ec)
					{
						co_return;
					}
				}

				auto const work = pool.generate (nano::work_version::work_1, root, difficulty);

				boost::property_tree::ptree answer;
				answer.put ("work", nano::to_string_hex (work.value_or (0)));
				answer.put ("hash", root.to_string ());
				response.body () = to_json (answer);
			}
			break;
			case fake_work_peer_mode::silent:
			{
				// Hold the connection until the other side closes it
				char byte{ 0 };
				co_await socket.async_read_some (asio::buffer (&byte, 1), asio::as_tuple (asio::use_awaitable));
				co_return;
			}
			case fake_work_peer_mode::http_error:
			{
				response.result (http::status::internal_server_error);
				response.body () = "Internal server error";
			}
			break;
			case fake_work_peer_mode::malformed:
			{
				response.body () = "this is not json";
			}
			break;
			case fake_work_peer_mode::garbled:
			{
				boost::property_tree::ptree answer;
				answer.put ("work", "not a number");
				answer.put ("hash", root.to_string ());
				response.body () = to_json (answer);
			}
			break;
			case fake_work_peer_mode::refusing:
			{
				boost::property_tree::ptree answer;
				answer.put ("error", "Cancelled");
				response.body () = to_json (answer);
			}
			break;
			case fake_work_peer_mode::insufficient:
			{
				boost::property_tree::ptree answer;
				answer.put ("work", nano::to_string_hex (insufficient_work (root, difficulty)));
				answer.put ("hash", root.to_string ());
				response.body () = to_json (answer);
			}
			break;
		}
	}

	response.prepare_payload ();
	co_await http::async_write (socket, response, asio::as_tuple (asio::use_awaitable));

	boost::system::error_code ignored;
	socket.shutdown (asio::ip::tcp::socket::shutdown_send, ignored);
}
