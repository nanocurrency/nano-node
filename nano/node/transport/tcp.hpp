#pragma once

#include <nano/node/common.hpp>
#include <nano/node/stats.hpp>
#include <nano/node/transport/transport.hpp>

namespace nano
{
namespace transport
{
	class channel_tcp : public nano::transport::channel
	{
	public:
		channel_tcp (nano::node &, std::shared_ptr<nano::socket>);
		~channel_tcp ();
		size_t hash_code () const override;
		bool operator== (nano::transport::channel const &) const override;
		void send_buffer (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) override;
		std::function<void(boost::system::error_code const &, size_t)> callback (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const override;
		std::string to_string () const override;
		bool operator== (nano::transport::channel_tcp const & other_a) const
		{
			return &node == &other_a.node && socket == other_a.socket;
		}
		std::shared_ptr<nano::socket> socket;

		nano::endpoint get_endpoint () const override
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			if (socket)
			{
				return nano::transport::map_tcp_to_endpoint (socket->remote_endpoint ());
			}
			else
			{
				return nano::endpoint (boost::asio::ip::address_v6::any (), 0);
			}
		}

		nano::tcp_endpoint get_tcp_endpoint () const override
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			if (socket)
			{
				return socket->remote_endpoint ();
			}
			else
			{
				return nano::tcp_endpoint (boost::asio::ip::address_v6::any (), 0);
			}
		}

		nano::transport::transport_type get_type () const override
		{
			return nano::transport::transport_type::tcp;
		}
	};
} // namespace transport
} // namespace nano
