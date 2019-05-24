#pragma once

#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>

#include <atomic>
#include <string>

namespace nano
{
namespace ipc
{
	/**
	 * The IPC framing format is simple: preamble followed by an encoding specific payload.
	 * Preamble is uint8_t {'N', encoding_type, reserved, reserved}. Reserved bytes MUST be zero.
	 * @note This is intentionally not an enum class as the values are only used as vector indices.
	 */
	enum preamble_offset
	{
		/** Always 'N' */
		lead = 0,
		/** One of the payload_encoding values */
		encoding = 1,
		/** Always zero */
		reserved_1 = 2,
		/** Always zero */
		reserved_2 = 3,
	};

	/** Abstract base type for sockets, implementing timer logic and a close operation */
	class socket_base
	{
	public:
		socket_base (boost::asio::io_context & io_ctx_a);
		virtual ~socket_base () = default;

		/** Close socket */
		virtual void close () = 0;

		/**
		 * Start IO timer.
		 * @param timeout_a Seconds to wait. To wait indefinitely, use std::chrono::seconds::max ()
		 */
		void timer_start (std::chrono::seconds timeout_a);
		void timer_expired ();
		void timer_cancel ();

	private:
		/** IO operation timer */
		boost::asio::deadline_timer io_timer;
	};

	/**
	 * Payload encodings; add protobuf, flatbuffers and so on as needed.
	 */
	enum class payload_encoding : uint8_t
	{
		/**
		 * Request is preamble followed by 32-bit BE payload length and payload bytes.
		 * Response is 32-bit BE payload length followed by payload bytes.
		 */
		json_legacy = 0x1,
		/** Request/response is same as json_legacy and exposes unsafe RPC's */
		json_unsafe = 0x2
	};

	/** IPC transport interface */
	class transport
	{
	public:
		virtual void stop () = 0;
		virtual ~transport () = default;
	};

	/** The domain socket file is attempted to be removed at both startup and shutdown. */
	class dsock_file_remover final
	{
	public:
		dsock_file_remover (std::string const & file_a);
		~dsock_file_remover ();

	private:
		std::string filename;
	};
}
}
