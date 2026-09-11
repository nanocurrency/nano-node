#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/property_tree/ptree.hpp>

#include <atomic>
#include <memory>

namespace nano::test
{
/**
 * Performs a single HTTP POST of a JSON document against a local port and captures the JSON response.
 * It knows nothing about the RPC API, so it serves both the `rpc_server` unit tests and the RPC API tests.
 *
 * The in-flight async operations hold a reference to this object, so it stays alive until the
 * request either completes or the io_context is destroyed. Callers are free to drop their handle
 * at any point, which is what happens whenever a test gives up on a request after its deadline
 * expires.
 */
class test_response : public std::enable_shared_from_this<test_response>
{
	// Only used to keep the constructor effectively private while remaining usable by `make_shared`
	struct private_tag
	{
	};

public:
	// Creates a response object without sending the request, use `run` to send it
	static std::shared_ptr<test_response> prepare (boost::property_tree::ptree const & request, boost::asio::io_context &);
	// Creates a response object and sends the request immediately
	static std::shared_ptr<test_response> send (boost::property_tree::ptree const & request, uint16_t port, boost::asio::io_context &);

	test_response (private_tag, boost::property_tree::ptree const & request, boost::asio::io_context &);

	void run (uint16_t port);

public:
	boost::property_tree::ptree const request;
	boost::asio::ip::tcp::socket sock;
	boost::property_tree::ptree json;
	boost::beast::flat_buffer sb;
	boost::beast::http::request<boost::beast::http::string_body> req;
	boost::beast::http::response<boost::beast::http::string_body> resp;
	std::atomic<int> status{ 0 };
};
}
