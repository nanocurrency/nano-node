#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/property_tree/ptree.hpp>

namespace nano::test
{
class test_response
{
public:
	test_response (boost::property_tree::ptree const & request_a, boost::asio::io_context & io_ctx_a);
	test_response (boost::property_tree::ptree const & request_a, uint16_t port_a, boost::asio::io_context & io_ctx_a);
	void run (uint16_t port_a);
	boost::property_tree::ptree const & request;
	boost::asio::ip::tcp::socket sock;
	boost::property_tree::ptree json;
	boost::beast::flat_buffer sb;
	boost::beast::http::request<boost::beast::http::string_body> req;
	boost::beast::http::response<boost::beast::http::string_body> resp;
	std::atomic<int> status{ 0 };
};
}
