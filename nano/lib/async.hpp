#pragma once

#include <nano/lib/utility.hpp>

#include <boost/asio.hpp>

#include <functional>
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

inline asio::awaitable<bool> cancelled ()
{
	auto state = co_await asio::this_coro::cancellation_state;
	co_return state.cancelled () != asio::cancellation_type::none;
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

class condition
{
public:
	explicit condition (nano::async::strand & strand) :
		strand{ strand },
		state{ std::make_shared<shared_state> (strand) }
	{
	}

	condition (condition &&) = default;

	condition & operator= (condition && other)
	{
		// Can only move if the strands are the same
		debug_assert (strand == other.strand);
		if (this != &other)
		{
			state = std::move (other.state);
		}
		return *this;
	}

	void notify ()
	{
		// Avoid unnecessary dispatch if already scheduled
		release_assert (state);
		if (state->scheduled.exchange (true) == false)
		{
			asio::dispatch (strand, [state_s = state] () {
				state_s->scheduled = false;
				state_s->timer.cancel ();
			});
		}
	}

	// Spuriously wakes up
	asio::awaitable<void> wait ()
	{
		debug_assert (strand.running_in_this_thread ());
		co_await wait_for (std::chrono::seconds{ 1 });
	}

	asio::awaitable<void> wait_for (auto duration)
	{
		debug_assert (strand.running_in_this_thread ());
		release_assert (state);
		state->timer.expires_after (duration);
		boost::system::error_code ec; // Swallow error from cancellation
		co_await state->timer.async_wait (asio::redirect_error (asio::use_awaitable, ec));
		debug_assert (!ec || ec == asio::error::operation_aborted);
	}

	void cancel ()
	{
		release_assert (state);
		asio::dispatch (strand, [state_s = state] () {
			state_s->scheduled = false;
			state_s->timer.cancel ();
		});
	}

	bool valid () const
	{
		return state != nullptr;
	}

	nano::async::strand & strand;

private:
	struct shared_state
	{
		asio::steady_timer timer;
		std::atomic<bool> scheduled{ false };

		explicit shared_state (nano::async::strand & strand) :
			timer{ strand } {};
	};
	std::shared_ptr<shared_state> state;
};

// Concept for awaitables
template <typename T>
concept async_task = std::same_as<T, asio::awaitable<void>>;

// Concept for callables that return an awaitable
template <typename T>
concept async_factory = requires (T t) {
	{
		t ()
	} -> std::same_as<asio::awaitable<void>>;
};

// Concept for callables that take a condition and return an awaitable
template <typename T>
concept async_factory_with_condition = requires (T t, condition & c) {
	{
		t (c)
	} -> std::same_as<asio::awaitable<void>>;
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

	template <async_task Func>
	task (nano::async::strand & strand, Func && func) :
		strand{ strand },
		cancellation{ strand }
	{
		future = asio::co_spawn (
		strand,
		std::forward<Func> (func),
		asio::bind_cancellation_slot (cancellation.slot (), asio::use_future));
	}

	template <async_factory Func>
	task (nano::async::strand & strand, Func && func) :
		strand{ strand },
		cancellation{ strand }
	{
		future = asio::co_spawn (
		strand,
		func (),
		asio::bind_cancellation_slot (cancellation.slot (), asio::use_future));
	}

	template <async_factory_with_condition Func>
	task (nano::async::strand & strand, Func && func) :
		strand{ strand },
		cancellation{ strand },
		condition{ std::make_unique<nano::async::condition> (strand) }
	{
		auto awaitable_func = func (*condition);
		future = asio::co_spawn (
		strand,
		func (*condition),
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
			condition = std::move (other.condition);
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
		if (condition)
		{
			condition->cancel ();
		}
	}

	void notify ()
	{
		if (condition)
		{
			condition->notify ();
		}
	}

	nano::async::strand & strand;

private:
	std::future<value_type> future;
	nano::async::cancellation cancellation;
	std::unique_ptr<nano::async::condition> condition;
};
}