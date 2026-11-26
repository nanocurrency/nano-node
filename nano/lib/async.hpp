#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/io_result.hpp>
#include <nano/lib/utility.hpp>

#include <boost/asio.hpp>

#include <functional>
#include <future>
#include <optional>
#include <string>
#include <system_error>

namespace asio = boost::asio;

namespace nano::async
{
using strand = asio::strand<asio::io_context::executor_type>;

// Whether the calling coroutine was cancelled; true only inside a `nano::async::task`, elsewhere asio throws operation_aborted at the co_await instead
inline asio::awaitable<bool> cancelled ()
{
	auto state = co_await asio::this_coro::cancellation_state;
	co_return state.cancelled () != asio::cancellation_type::none;
}

// Waits for the duration; operation_aborted when the coroutine is cancelled before or during the wait
inline asio::awaitable<nano::io_result<void>> sleep_for (auto duration)
{
	// A cancellation only ends the wait in progress when it arrives, so an earlier one is looked up here
	if (co_await nano::async::cancelled ())
	{
		co_return asio::error::operation_aborted;
	}

	asio::steady_timer timer{ co_await asio::this_coro::executor, duration };
	co_return co_await timer.async_wait (nano::async::use_result);
}

// How a coroutine run through `settle` ended
struct settled
{
	boost::system::error_code ec; // Error it returned or threw, operation_aborted when cancelled, error_common::exception for an exception without a code
	std::optional<std::string> unexpected; // Message of any exception other than a cancellation, which nothing is expected to throw
};

// Awaits a coroutine to its end and reports how it ended, letting no exception through
template <typename Result>
	requires std::same_as<Result, void> || std::same_as<Result, nano::io_result<void>>
asio::awaitable<nano::async::settled> settle (asio::awaitable<Result> body)
{
	nano::async::settled ended;
	try
	{
		if constexpr (std::same_as<Result, void>)
		{
			co_await std::move (body);
		}
		else
		{
			auto const result = co_await std::move (body);
			ended.ec = result.error (); // Empty on success
		}
	}
	catch (boost::system::system_error const & ex)
	{
		ended.ec = ex.code ();
		// A cancelled coroutine throws operation_aborted out of the operation it was waiting on
		if (ex.code () != asio::error::operation_aborted)
		{
			ended.unexpected = ex.what ();
		}
	}
	catch (std::system_error const & ex)
	{
		// Never a cancellation, asio throws the boost type
		ended.ec = ex.code ();
		ended.unexpected = ex.what ();
	}
	catch (std::exception const & ex)
	{
		ended.ec = std::error_code{ nano::error_common::exception };
		ended.unexpected = ex.what ();
	}
	catch (...)
	{
		ended.ec = std::error_code{ nano::error_common::exception };
		ended.unexpected = "unknown exception";
	}
	co_return ended;
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
	std::future<void> emit (asio::cancellation_type type = asio::cancellation_type::all)
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
 * Uses std::shared_future for multiple waiters capability.
 *
 * Cancellation is never thrown into the coroutine: asio's default of throwing operation_aborted from the next co_await of a cancelled coroutine is turned off for the task's coroutine and everything it awaits.
 * `cancel ()` ends the wait in progress with operation_aborted and makes `nano::async::cancelled ()` return true from then on. A wait started later is not ended, unless it looks the cancellation up itself, as `nano::async::sleep_for` does.
 * The coroutine has to return on its own, by checking the results of its waits and `cancelled ()` in its loops; one that does neither never finishes and blocks `join ()`.
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
		std::future<value_type> fut = asio::co_spawn (
		strand,
		run (std::forward<Func> (func)),
		asio::bind_cancellation_slot (cancellation.slot (), asio::use_future));

		// Convert to shared_future
		future = fut.share ();
	}

	template <async_factory Func>
	task (nano::async::strand & strand, Func && func) :
		strand{ strand },
		cancellation{ strand }
	{
		auto fut = asio::co_spawn (
		strand,
		run (func ()),
		asio::bind_cancellation_slot (cancellation.slot (), asio::use_future));

		future = fut.share (); // Convert to shared_future
	}

	template <async_factory_with_condition Func>
	task (nano::async::strand & strand, Func && func) :
		strand{ strand },
		cancellation{ strand },
		condition{ std::make_unique<nano::async::condition> (strand) }
	{
		auto fut = asio::co_spawn (
		strand,
		run (func (*condition)),
		asio::bind_cancellation_slot (cancellation.slot (), asio::use_future));

		future = fut.share (); // Convert to shared_future
	}

	~task ()
	{
		release_assert (!running (), "async task not joined before destruction");
	}

	task (task && other) = default;

	task & operator= (task && other)
	{
		// Can only move if the strands are the same
		debug_assert (strand == other.strand);
		if (this != &other)
		{
			// Overwriting a running task would detach its coroutine from the only handle that can cancel or join it
			release_assert (!running (), "async task not joined before move assignment");
			future = std::move (other.future);
			cancellation = std::move (other.cancellation);
			condition = std::move (other.condition);
		}
		return *this;
	}

public:
	std::shared_future<value_type> get_future () const
	{
		return future; // Give a copy of the shared future
	}

	bool joinable () const
	{
		return future.valid ();
	}

	bool ready () const
	{
		release_assert (future.valid ());
		return get_future ().wait_for (std::chrono::seconds{ 0 }) == std::future_status::ready;
	}

	bool running () const
	{
		return joinable () && !ready ();
	}

	// Waits for the task to finish; waiting on a running task needs the strand's io_context running and a thread outside it
	void join ()
	{
		// Only a task that was given a coroutine has a future to wait on
		release_assert (future.valid ());
		// A finished task has nothing left to wait for, so it can be joined from any thread, any number of times, even after shutdown
		if (ready ())
		{
			return;
		}
		// The completion is posted to the strand, so a stopped io_context would never deliver it
		release_assert (!strand.get_inner_executor ().context ().stopped (), "async task joined after its io_context stopped");
		// Waiting on an io thread of that context can block the very thread the completion needs
		release_assert (!strand.get_inner_executor ().running_in_this_thread (), "async task joined on a thread of its io_context");
		// Blocks until the coroutine finishes or is cancelled
		get_future ().wait ();
	}

	// Asks the coroutine to stop: its wait in progress ends with operation_aborted, nothing is thrown into it
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
	// Runs the coroutine with asio's cancellation throw turned off, a setting that covers everything it awaits
	static asio::awaitable<void> run (asio::awaitable<void> body)
	{
		co_await asio::this_coro::throw_if_cancelled (false);
		co_await std::move (body);
	}

private:
	std::shared_future<value_type> future;
	nano::async::cancellation cancellation;
	std::unique_ptr<nano::async::condition> condition; // Optional, but needs a fixed address
};
}