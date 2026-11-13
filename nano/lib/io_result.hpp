#pragma once

#include <boost/asio/associator.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>

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

// Completion handler that folds the error code and value it is called with into a `nano::io_result` for the handler it wraps
template <typename Handler>
struct as_result_handler
{
	Handler handler;

	void operator() (boost::system::error_code const &);
	template <typename T>
	void operator() (boost::system::error_code const &, T &&);
};

// Completion signature of an operation once its error code and value are folded into a `nano::io_result`
template <typename Signature>
struct as_result_signature;

template <typename R>
struct as_result_signature<R (boost::system::error_code)>
{
	using type = R (nano::io_result<void>);
};

template <typename R, typename T>
struct as_result_signature<R (boost::system::error_code, T)>
{
	using type = R (nano::io_result<std::decay_t<T>>);
};

// Operation initiation that keeps the members of the one it wraps, such as its executor, and passes it the handler wrapped in `as_result_handler`
template <typename Initiation>
struct as_result_initiation : Initiation
{
	explicit as_result_initiation (Initiation);

	template <typename Handler, typename... Args>
	void operator() (Handler &&, Args &&...) &&;
};
}

/*
 * The hooks asio looks up for a completion token, specialized in its namespace as the language requires.
 * Names from nano are spelled `::nano` here and in their definitions, since inside boost `nano` is the `boost::nano` ratio
 */

namespace boost::asio
{
// Initiates an operation adapted by `as_result` with the wrapped token, under the folded completion signature
template <typename CompletionToken, typename Signature>
struct async_result<::nano::async::as_result_t<CompletionToken>, Signature> : async_result<CompletionToken, typename ::nano::async::as_result_signature<Signature>::type>
{
	template <typename Initiation, typename RawCompletionToken, typename... Args>
	static auto initiate (Initiation &&, RawCompletionToken &&, Args &&...);
};

// Forwards the executor, allocator and cancellation slot of the wrapped handler, so cancelling an awaiting coroutine still cancels the operation
template <template <typename, typename> class Associator, typename Handler, typename DefaultCandidate>
struct associator<Associator, ::nano::async::as_result_handler<Handler>, DefaultCandidate> : Associator<Handler, DefaultCandidate>
{
	static typename Associator<Handler, DefaultCandidate>::type get (::nano::async::as_result_handler<Handler> const &) noexcept;
	static auto get (::nano::async::as_result_handler<Handler> const &, DefaultCandidate const &) noexcept -> decltype (Associator<Handler, DefaultCandidate>::get (std::declval<Handler const &> (), std::declval<DefaultCandidate const &> ()));
};
}

/*
 * Definitions
 */

template <typename Handler>
void ::nano::async::as_result_handler<Handler>::operator() (boost::system::error_code const & ec)
{
	if (ec)
	{
		std::move (handler) (::nano::io_result<void>{ boost::system::in_place_error, ec });
	}
	else
	{
		std::move (handler) (::nano::io_success ());
	}
}

template <typename Handler>
template <typename T>
void nano::async::as_result_handler<Handler>::operator() (boost::system::error_code const & ec, T && value)
{
	using result_type = nano::io_result<std::decay_t<T>>;
	if (ec)
	{
		std::move (handler) (result_type{ boost::system::in_place_error, ec });
	}
	else
	{
		std::move (handler) (result_type{ boost::system::in_place_value, std::forward<T> (value) });
	}
}

template <typename Initiation>
nano::async::as_result_initiation<Initiation>::as_result_initiation (Initiation initiation_a) :
	Initiation{ std::move (initiation_a) }
{
}

template <typename Initiation>
template <typename Handler, typename... Args>
void nano::async::as_result_initiation<Initiation>::operator() (Handler && handler, Args &&... args) &&
{
	static_cast<Initiation &&> (*this) (nano::async::as_result_handler<std::decay_t<Handler>>{ std::forward<Handler> (handler) }, std::forward<Args> (args)...);
}

template <typename CompletionToken, typename Signature>
template <typename Initiation, typename RawCompletionToken, typename... Args>
auto boost::asio::async_result<::nano::async::as_result_t<CompletionToken>, Signature>::initiate (Initiation && initiation, RawCompletionToken && token, Args &&... args)
{
	// A const token, such as `use_result`, hands on its wrapped token as const
	using inner_token = std::conditional_t<std::is_const_v<std::remove_reference_t<RawCompletionToken>>, CompletionToken const, CompletionToken>;
	using inner_signature = typename ::nano::async::as_result_signature<Signature>::type;

	return boost::asio::async_initiate<inner_token, inner_signature> (
	::nano::async::as_result_initiation<std::decay_t<Initiation>>{ std::forward<Initiation> (initiation) },
	token.token,
	std::forward<Args> (args)...);
}

template <template <typename, typename> class Associator, typename Handler, typename DefaultCandidate>
auto boost::asio::associator<Associator, ::nano::async::as_result_handler<Handler>, DefaultCandidate>::get (::nano::async::as_result_handler<Handler> const & handler) noexcept -> typename Associator<Handler, DefaultCandidate>::type
{
	return Associator<Handler, DefaultCandidate>::get (handler.handler);
}

template <template <typename, typename> class Associator, typename Handler, typename DefaultCandidate>
auto boost::asio::associator<Associator, ::nano::async::as_result_handler<Handler>, DefaultCandidate>::get (::nano::async::as_result_handler<Handler> const & handler, DefaultCandidate const & candidate) noexcept -> decltype (Associator<Handler, DefaultCandidate>::get (std::declval<Handler const &> (), std::declval<DefaultCandidate const &> ()))
{
	return Associator<Handler, DefaultCandidate>::get (handler.handler, candidate);
}
