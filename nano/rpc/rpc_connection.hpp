#pragma once

#include <atomic>
#include <boost/beast.hpp>
#include <nano/node/node.hpp>

namespace nano
{
class rpc;
class rpc_connection : public std::enable_shared_from_this<nano::rpc_connection>
{
public:
	rpc_connection (nano::node &, nano::rpc &);
	virtual ~rpc_connection () = default;
	virtual void parse_connection ();
	virtual void write_completion_handler (std::shared_ptr<nano::rpc_connection> rpc_connection);
	virtual void prepare_head (unsigned version, boost::beast::http::status status = boost::beast::http::status::ok);
	virtual void write_result (std::string body, unsigned version, boost::beast::http::status status = boost::beast::http::status::ok);
	void read ();
	std::shared_ptr<nano::node> node;
	nano::rpc & rpc;
	boost::asio::ip::tcp::socket socket;
	boost::beast::flat_buffer buffer;
	boost::beast::http::response<boost::beast::http::string_body> res;
	std::atomic_flag responded;
};
}
