#pragma once

#include <boost/asio/associator.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>

#include <memory>
#include <type_traits>
#include <utility>

namespace nano
{
// Outcome of an I/O operation: its value, or the error code it failed with
template <typename T>
using io_result = boost::system::result<T>;

// Successful outcome of an I/O operation that produces no value
inline nano::io_result<void> io_success ()
{
	return {};
}
}

namespace nano::async
{
/**
 * Completion token adapter that hands the wrapped token an operation's outcome as one `nano::io_result` instead of an error code and a value.
 * An operation completing with `void (error_code)` completes with `nano::io_result<void>`, one completing with `void (error_code, T)` with `nano::io_result<T>`
 */
template <typename CompletionToken>
struct as_result_t
{
	CompletionToken token;
};

// Adapts a completion token to receive the outcome as a `nano::io_result`
template <typename CompletionToken>
constexpr nano::async::as_result_t<std::decay_t<CompletionToken>> as_result (CompletionToken && token)
{
	return { std::forward<CompletionToken> (token) };
}

// Completion token that makes an operation awaitable as a `nano::io_result`, so a failed operation returns its error code instead of throwing
inline constexpr auto use_result = nano::async::as_result (boost::asio::use_awaitable);

namespace detail
{
	template <typename R, typename Value = void>
	struct folded_signature
	{
		using result = nano::io_result<std::decay_t<Value>>;
		using type = R (result);
	};

	template <typename Result, typename Handler>
	struct result_handler
	{
		Handler handler;

		template <typename... Values>
		void operator() (boost::system::error_code const & ec, Values &&... values)
		{
			// Construct the result type promised by the operation's signature.
			std::move (handler) (ec ? Result{ boost::system::in_place_error, ec }
									: Result{ boost::system::in_place_value, std::forward<Values> (values)... });
		}

		friend bool asio_handler_is_continuation (result_handler * self)
		{
			using boost::asio::asio_handler_is_continuation;
			return asio_handler_is_continuation (std::addressof (self->handler));
		}
	};

	template <typename Result>
	struct start_result
	{
		template <typename Handler, typename Init, typename... Args>
		void operator() (Handler && handler, Init && init, Args &&... args) const &
		{
			std::forward<Init> (init) (
			result_handler<Result, std::decay_t<Handler>>{ std::forward<Handler> (handler) },
			std::forward<Args> (args)...);
		}

		template <typename Handler, typename Init, typename... Args>
		void operator() (Handler && handler, Init && init, Args &&... args) &&
		{
			// Replaying a const deferred operation supplies its saved arguments as lvalues.
			// Give this invocation its own copy of the saved initiator.
			if constexpr (std::is_lvalue_reference_v<Init>)
			{
				std::as_const (*this) (std::forward<Handler> (handler), std::decay_t<Init> (init), std::forward<Args> (args)...);
			}
			else
			{
				std::as_const (*this) (std::forward<Handler> (handler), std::forward<Init> (init), std::forward<Args> (args)...);
			}
		}
	};

	template <typename Result, typename Init>
	auto make_start (Init const & init)
	{
		if constexpr (requires { typename Init::executor_type; init.get_executor (); })
		{
			return boost::asio::bind_executor (init.get_executor (), start_result<Result>{});
		}
		else
		{
			return start_result<Result>{};
		}
	}
} // namespace detail
} // namespace nano::async

namespace boost::asio
{
template <template <typename, typename> class Associator, typename Result, typename Handler, typename Candidate>
struct associator<Associator, ::nano::async::detail::result_handler<Result, Handler>, Candidate> : Associator<Handler, Candidate>
{
	using wrapper = ::nano::async::detail::result_handler<Result, Handler>;
	using base = Associator<Handler, Candidate>;

	static decltype (auto) get (wrapper const & h) noexcept
	{
		return base::get (h.handler);
	}

	static decltype (auto) get (wrapper const & h, Candidate const & c) noexcept
	{
		return base::get (h.handler, c);
	}
};

template <typename Token, typename R, typename Error, typename... Values>
	requires (sizeof...(Values) <= 1 && std::is_same_v<std::remove_const_t<std::remove_reference_t<Error>>, boost::system::error_code>)
struct async_result<::nano::async::as_result_t<Token>, R (Error, Values...)> : async_result<Token, typename ::nano::async::detail::folded_signature<R, Values...>::type>
{
	using folded = ::nano::async::detail::folded_signature<R, Values...>;
	template <typename Init>
	using starter = decltype (::nano::async::detail::make_start<typename folded::result> (std::declval<Init &> ()));

	template <typename Init, typename RawToken, typename... Args>
	static auto initiate (Init && init, RawToken && token, Args &&... args)
	-> decltype (async_initiate<typename folded::type> (
	std::declval<starter<Init> const &> (),
	std::forward<RawToken> (token).token, std::forward<Init> (init), std::forward<Args> (args)...))
	{
		auto const start = ::nano::async::detail::make_start<typename folded::result> (init);
		return async_initiate<typename folded::type> (
		start, std::forward<RawToken> (token).token, std::forward<Init> (init), std::forward<Args> (args)...);
	}
};
} // namespace boost::asio
