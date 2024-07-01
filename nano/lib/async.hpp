#pragma once

#include <nano/lib/utility.hpp>

#include <boost/asio.hpp>

#include <future>

namespace asio = boost::asio;

namespace nano::async
{
using strand = asio::strand<asio::io_context::executor_type>;

inline asio::awaitable<void> sleep_for (auto duration)
{
	asio::steady_timer timer{ co_await asio::this_coro::executor };
	timer.expires_after (duration);
	boost::system::error_code ec; // Swallow potential error from coroutine cancellation
	co_await timer.async_wait (asio::redirect_error (asio::use_awaitable, ec));
	debug_assert (!ec || ec == asio::error::operation_aborted);
}

/**
 * A cancellation signal that can be emitted from any thread.
 * It follows the same semantics as asio::cancellation_signal.
 */
class cancellation
{
public:
	explicit cancellation (nano::async::strand & strand) :
		strand{ strand },
		signal{ std::make_shared<asio::cancellation_signal> () }
	{
	}

	cancellation (cancellation && other) = default;

	cancellation & operator= (cancellation && other)
	{
		// Can only move if the strands are the same
		debug_assert (strand == other.strand);

		if (this != &other)
		{
			signal = std::move (other.signal);
		}
		return *this;
	};

public:
	auto emit (asio::cancellation_type type = asio::cancellation_type::all)
	{
		return asio::dispatch (strand, asio::use_future ([signal_l = signal, type] () {
			signal_l->emit (type);
		}));
	}

	auto slot ()
	{
		// Ensure that the slot is only connected once
		debug_assert (std::exchange (slotted, true) == false);
		return signal->slot ();
	}

	nano::async::strand & strand;

private:
	std::shared_ptr<asio::cancellation_signal> signal;
	bool slotted{ false }; // For debugging purposes
};

/**
 * Wrapper with convenience functions and safety checks for asynchronous tasks.
 * Aims to provide interface similar to std::thread.
 */
class task
{
public:
	// Only thread-like void tasks are supported for now
	using value_type = void;

	explicit task (nano::async::strand & strand) :
		strand{ strand },
		cancellation{ strand }
	{
	}

	task (nano::async::strand & strand, auto && func) :
		strand{ strand },
		cancellation{ strand }
	{
		future = asio::co_spawn (
		strand,
		std::forward<decltype (func)> (func),
		asio::bind_cancellation_slot (cancellation.slot (), asio::use_future));
	}

	~task ()
	{
		release_assert (!joinable () || ready (), "async task not joined before destruction");
	}

	task (task && other) = default;

	task & operator= (task && other)
	{
		// Can only move if the strands are the same
		debug_assert (strand == other.strand);

		if (this != &other)
		{
			future = std::move (other.future);
			cancellation = std::move (other.cancellation);
		}
		return *this;
	}

public:
	bool joinable () const
	{
		return future.valid ();
	}

	bool ready () const
	{
		release_assert (future.valid ());
		return future.wait_for (std::chrono::seconds{ 0 }) == std::future_status::ready;
	}

	void join ()
	{
		release_assert (future.valid ());
		future.wait ();
		future = {};
	}

	void cancel ()
	{
		debug_assert (joinable ());
		cancellation.emit ();
	}

	nano::async::strand & strand;

private:
	std::future<value_type> future;
	nano::async::cancellation cancellation;
};
}