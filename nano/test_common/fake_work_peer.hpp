#pragma once

#include <nano/lib/async.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/work_request.hpp>

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <deque>

namespace nano::test
{
// How a fake work peer treats the `work_generate` requests it receives
enum class fake_work_peer_mode
{
	good, // Answers with work that reaches the requested difficulty
	slow, // Answers like `good`, after a delay
	silent, // Reads the request and never answers
	dropping, // Reads the request and closes the connection without answering
	http_error, // Answers with HTTP status 500
	malformed, // Answers with a body that is not JSON
	garbled, // Answers with JSON whose work is not a number
	refusing, // Answers with an error, the way a work server declines a request
	insufficient, // Answers with work that does not reach the requested difficulty
};

/**
 * A work server for tests that listens on the loopback interface and answers the work RPC according to its mode.
 * It generates work on a pool of its own, so that nothing the node under test does to its pool reaches the peer.
 * The io_context has to be running for as long as the peer lives, and `good` blocks one of its threads while it generates
 */
class fake_work_peer final
{
public:
	fake_work_peer (asio::io_context &, fake_work_peer_mode, std::chrono::milliseconds delay = std::chrono::milliseconds{ 500 });
	~fake_work_peer ();

	// Closes the listening socket and every connection, called by the destructor
	void stop ();

	// The mode applies to the requests received from now on
	void set_mode (fake_work_peer_mode);

	// The peer the way a node lists it in its config
	nano::work_peer peer () const;
	uint16_t port () const;

	// Number of `work_generate` requests received
	size_t generate_count () const;
	// Number of `work_cancel` requests received
	size_t cancel_count () const;
	// Number of `work_generate` requests received that named an account
	size_t account_count () const;

private:
	asio::awaitable<void> run ();
	asio::awaitable<void> serve (asio::ip::tcp::socket);

private:
	std::chrono::milliseconds const delay; // How long `slow` waits before it answers

	nano::work_pool pool;
	nano::async::strand strand;
	asio::ip::tcp::acceptor acceptor;
	uint16_t local_port{ 0 };

	std::atomic<fake_work_peer_mode> mode;
	std::atomic<size_t> generates{ 0 };
	std::atomic<size_t> cancels{ 0 };
	std::atomic<size_t> accounts{ 0 };
	std::atomic<bool> stopped{ false };

	nano::async::task task;
	// Only the accept loop adds to it, and `stop` reads it once that loop has ended
	std::deque<nano::async::task> connections;
};
}
