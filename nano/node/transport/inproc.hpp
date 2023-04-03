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
			std::size_t hash_code () const override;
			bool operator== (nano::transport::channel const &) const override;

			// TODO: investigate clang-tidy warning about default parameters on virtual/override functions
			void send_buffer (nano::shared_const_buffer const &, std::function<void (boost::system::error_code const &, std::size_t)> const & = nullptr, nano::transport::buffer_drop_policy = nano::transport::buffer_drop_policy::limiter, nano::transport::traffic_type = nano::transport::traffic_type::generic) override;

			std::string to_string () const override;
			bool operator== (nano::transport::inproc::channel const & other_a) const
			{
				return endpoint == other_a.get_endpoint ();
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
				return nano::transport::transport_type::loopback;
			}

		private:
			nano::node & destination;
			nano::endpoint const endpoint;
		};
	} // namespace inproc
} // namespace transport
} // namespace nano
