#pragma once

#include <celerix/node/transport/channel.hpp>
#include <celerix/node/transport/transport.hpp>

namespace celerix
{
namespace transport
{
	/**
	 * In-process transport channel. Mostly useful for unit tests
	 **/
	namespace inproc
	{
		class channel final : public celerix::transport::channel
		{
		public:
			explicit channel (celerix::node & node, celerix::node & destination);

			std::string to_string () const override;

			celerix::endpoint get_remote_endpoint () const override
			{
				return endpoint;
			}

			celerix::endpoint get_local_endpoint () const override
			{
				return endpoint;
			}

			celerix::transport::transport_type get_type () const override
			{
				return celerix::transport::transport_type::loopback;
			}

			void close () override
			{
				// Can't be closed
			}

		protected:
			bool send_buffer (celerix::shared_const_buffer const &, celerix::transport::traffic_type, celerix::transport::channel::callback_t) override;

		private:
			celerix::node & destination;
			celerix::endpoint const endpoint;
		};
	} // namespace inproc
} // namespace transport
} // namespace celerix
