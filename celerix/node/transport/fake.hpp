#pragma once

#include <celerix/node/transport/channel.hpp>
#include <celerix/node/transport/transport.hpp>

namespace celerix
{
namespace transport
{
	/**
	 * Fake channel that connects to nothing and allows its attributes to be manipulated. Mostly useful for unit tests.
	 **/
	namespace fake
	{
		class channel final : public celerix::transport::channel
		{
		public:
			explicit channel (celerix::node &);

			std::string to_string () const override;

			void set_endpoint (celerix::endpoint const & endpoint_a)
			{
				endpoint = endpoint_a;
			}

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
				return celerix::transport::transport_type::fake;
			}

			void close () override
			{
				closed = true;
			}

			bool alive () const override
			{
				return !closed;
			}

		protected:
			bool send_buffer (celerix::shared_const_buffer const &, celerix::transport::traffic_type, celerix::transport::channel::callback_t) override;

		private:
			celerix::endpoint endpoint;

			std::atomic<bool> closed{ false };
		};
	} // namespace fake
} // namespace transport
} // namespace celerix
