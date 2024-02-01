#pragma once

#include <nano/ipc_flatbuffers_lib/flatbuffer_producer.hpp>
#include <nano/lib/asio.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/ipc.hpp>
#include <nano/lib/utility.hpp>

#include <chrono>
#include <string>
#include <vector>

#include <flatbuffers/flatbuffers.h>

namespace nano
{
class shared_const_buffer;
namespace ipc
{
	class ipc_client_impl
	{
	public:
		virtual ~ipc_client_impl () = default;
	};

	/** IPC client */
	class ipc_client
	{
	public:
		ipc_client (boost::asio::io_context & io_ctx_a);
		ipc_client (ipc_client && ipc_client) = default;
		virtual ~ipc_client () = default;

		/** Connect to a domain socket */
		nano::error connect (std::string const & path);

		/** Connect to a tcp socket synchronously */
		nano::error connect (std::string const & host, uint16_t port);

		/** Connect to a tcp socket asynchronously */
		void async_connect (std::string const & host, uint16_t port, std::function<void (nano::error)> callback);

		/** Write buffer asynchronously */
		void async_write (nano::shared_const_buffer const & buffer_a, std::function<void (nano::error, size_t)> callback_a);

		/** Read \p size_a bytes asynchronously */
		void async_read (std::shared_ptr<std::vector<uint8_t>> const & buffer_a, size_t size_a, std::function<void (nano::error, size_t)> callback_a);

		/**
		 * Read a length-prefixed message asynchronously using the given timeout. This is suitable for full duplex scenarios where it may
		 * take an arbitrarily long time for the node to send messages for a given subscription.
		 * Received length must be a big endian 32-bit unsigned integer.
		 * @param buffer_a Receives the payload
		 * @param timeout_a How long to await message data. In some scenarios, such as waiting for data on subscriptions, specifying std::chrono::seconds::max() makes sense.
		 * @param callback_a If called without errors, the payload buffer is successfully populated
		 */
		void async_read_message (std::shared_ptr<std::vector<uint8_t>> const & buffer_a, std::chrono::seconds timeout_a, std::function<void (nano::error, size_t)> callback_a);

	private:
		boost::asio::io_context & io_ctx;

		// PIMPL pattern to hide implementation details
		std::unique_ptr<ipc_client_impl> impl;
	};

	/** Convenience function for making synchronous IPC calls. The client must be connected */
	std::string request (nano::ipc::payload_encoding encoding_a, nano::ipc::ipc_client & ipc_client, std::string const & rpc_action_a);

	/**
	 * Returns a buffer with an IPC preamble for the given \p encoding_a
	 */
	std::vector<uint8_t> get_preamble (nano::ipc::payload_encoding encoding_a);

	/**
	 * Returns a buffer with an IPC preamble, followed by 32-bit BE lenght, followed by payload
	 */
	nano::shared_const_buffer prepare_flatbuffers_request (std::shared_ptr<flatbuffers::FlatBufferBuilder> const & flatbuffer_a);

	template <typename T>
	nano::shared_const_buffer shared_buffer_from (T & object_a, std::string const & correlation_id_a = {}, std::string const & credentials_a = {})
	{
		auto buffer_l (nano::ipc::flatbuffer_producer::make_buffer (object_a, correlation_id_a, credentials_a));
		return nano::ipc::prepare_flatbuffers_request (buffer_l);
	}

	/**
	 * Returns a buffer with an IPC preamble for the given \p encoding_a followed by the payload. Depending on encoding,
	 * the buffer may contain a payload length or end sentinel.
	 */
	nano::shared_const_buffer prepare_request (nano::ipc::payload_encoding encoding_a, std::string const & payload_a);
}
}
