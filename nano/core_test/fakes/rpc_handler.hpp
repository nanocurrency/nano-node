#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/rpc_handler_interface.hpp>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace nano::test
{
/** Echoes every request body back, so the server side can be tested without any RPC API */
class echo_rpc_handler final : public nano::rpc_handler_interface
{
public:
	void process_request (std::string const &, std::string const & body, std::function<void (std::string const &)> response) override
	{
		++requests;
		response (body);
	}

	void process_request_v2 (nano::rpc_handler_request_params const &, std::string const & body, std::function<void (std::shared_ptr<std::string> const &)> response) override
	{
		++requests;
		response (std::make_shared<std::string> (body));
	}

	std::atomic<int> requests{ 0 };
};

/** Holds every request until the test releases it, to keep connections in flight */
class deferred_rpc_handler final : public nano::rpc_handler_interface
{
public:
	void process_request (std::string const &, std::string const &, std::function<void (std::string const &)> response) override
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		pending.push_back (std::move (response));
	}

	void process_request_v2 (nano::rpc_handler_request_params const &, std::string const &, std::function<void (std::shared_ptr<std::string> const &)> response) override
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		pending.push_back ([response = std::move (response)] (std::string const & body) {
			response (std::make_shared<std::string> (body));
		});
	}

	std::size_t pending_count ()
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		return pending.size ();
	}

	void respond_all (std::string const & body)
	{
		decltype (pending) released;
		{
			nano::lock_guard<nano::mutex> lock{ mutex };
			released.swap (pending);
		}
		for (auto & response : released)
		{
			response (body);
		}
	}

private:
	nano::mutex mutex;
	std::deque<std::function<void (std::string const &)>> pending;
};
}
