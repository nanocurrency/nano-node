#pragma once

#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>

namespace nano
{
namespace transport
{
	/**
	 * Fake channel that connects to nothing and allows its attributes to be manipulated. Mostly useful for unit tests.
	 **/
	namespace fake
	{
		class channel final : public nano::transport::channel
		{
		public:
			explicit channel (nano::node &);

			std::string to_string () const override;

			void send_buffer (
			nano::shared_const_buffer const &,
			std::function<void (boost::system::error_code const &, std::size_t)> const & = nullptr,
			nano::transport::buffer_drop_policy = nano::transport::buffer_drop_policy::limiter,
			nano::transport::traffic_type = nano::transport::traffic_type::generic) override;

			void set_endpoint (nano::endpoint const & endpoint_a)
			{
				endpoint = endpoint_a;
			}

			nano::endpoint get_endpoint () const override
			{
				return endpoint;
			}

			nano::tcp_endpoint get_tcp_endpoint () const override
			{
				return nano::transport::map_endpoint_to_tcp (endpoint);
			}

			nano::transport::transport_type get_type () const override
			{
				return nano::transport::transport_type::fake;
			}

			void close () override
			{
				closed = true;
			}

			bool alive () const override
			{
				return !closed;
			}

		private:
			nano::endpoint endpoint;

			std::atomic<bool> closed{ false };
		};
	} // namespace fake
} // namespace transport
} // namespace nano
