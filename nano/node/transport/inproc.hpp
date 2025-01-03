#pragma once

#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>

namespace nano
{
namespace transport
{
	/**
	 * In-process transport channel. Mostly useful for unit tests
	 **/
	namespace inproc
	{
		class channel final : public nano::transport::channel
		{
		public:
			explicit channel (nano::node & node, nano::node & destination);

			std::string to_string () const override;

			nano::endpoint get_remote_endpoint () const override
			{
				return endpoint;
			}

			nano::endpoint get_local_endpoint () const override
			{
				return endpoint;
			}

			nano::transport::transport_type get_type () const override
			{
				return nano::transport::transport_type::loopback;
			}

			void close () override
			{
				// Can't be closed
			}

		protected:
			bool send_buffer (nano::shared_const_buffer const &, nano::transport::traffic_type, nano::transport::channel::callback_t) override;

		private:
			nano::node & destination;
			nano::endpoint const endpoint;
		};
	} // namespace inproc
} // namespace transport
} // namespace nano
