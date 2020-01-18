#pragma once

#include <nano/boost/asio/ip/tcp.hpp>
#include <nano/boost/asio/strand.hpp>
#include <nano/boost/beast/core/flat_buffer.hpp>
#include <nano/boost/beast/http/string_body.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/common.hpp>

#include <boost/optional.hpp>

#include <mutex>

using request_type = boost::beast::http::request<boost::beast::http::string_body>;

namespace boost
{
namespace asio
{
	class io_context;
}
}

namespace nano
{
class node;

struct work_request final
{
	nano::root root;
	uint64_t difficulty;
	boost::optional<nano::account> const account;
	std::function<void(boost::optional<uint64_t>)> callback;
	std::vector<std::pair<std::string, uint16_t>> const peers;
};

/**
 * distributed_work cancels local and peer work requests when going out of scope
 */
class distributed_work final : public std::enable_shared_from_this<nano::distributed_work>
{
	enum class work_generation_status
	{
		ongoing,
		success,
		cancelled,
		failure_local,
		failure_peers
	};

	class peer_request final
	{
	public:
		peer_request (boost::asio::io_context & io_ctx_a, nano::tcp_endpoint const & endpoint_a) :
		endpoint (endpoint_a),
		socket (io_ctx_a)
		{
		}
		std::shared_ptr<request_type> get_prepared_json_request (std::string const &) const;
		nano::tcp_endpoint const endpoint;
		boost::beast::flat_buffer buffer;
		boost::beast::http::response<boost::beast::http::string_body> response;
		boost::asio::ip::tcp::socket socket;
	};

public:
	distributed_work (nano::node &, nano::work_request const &, std::chrono::seconds const &);
	~distributed_work ();
	void start ();
	void cancel ();

private:
	void start_work ();
	/** Cancellation is done with an entirely new connection, \p request_a is only used to copy its address and port */
	void cancel (peer_request const & request_a);
	/** Called on a successful peer response, validates the reply */
	void success (std::string const &, nano::tcp_endpoint const &);
	/** Send a work_cancel message to all remaining connections */
	void stop_once (bool const);
	void set_once (uint64_t const, std::string const & source_a = "local");
	void failure (nano::tcp_endpoint const &);
	void handle_failure (bool const);
	bool remove (nano::tcp_endpoint const &);
	void add_bad_peer (nano::tcp_endpoint const &);

	nano::node & node;
	nano::work_request request;

	std::chrono::seconds backoff;
	boost::asio::strand<boost::asio::io_context::executor_type> strand;
	std::vector<std::pair<std::string, uint16_t>> need_resolve;
	std::vector<nano::tcp_endpoint> outstanding;
	std::vector<std::weak_ptr<peer_request>> connections;

	work_generation_status status{ work_generation_status::ongoing };
	uint64_t work_result{ 0 };

	nano::timer<std::chrono::milliseconds> elapsed; // logging only
	std::vector<std::string> bad_peers; // websocket
	std::string winner; // websocket

	std::mutex mutex;
	std::atomic<bool> finished{ false };
	std::atomic<bool> stopped{ false };
	std::atomic<bool> local_generation_started{ false };
};
}