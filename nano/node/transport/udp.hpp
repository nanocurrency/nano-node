#pragma once

#include <nano/node/common.hpp>
#include <nano/node/stats.hpp>
#include <nano/node/transport/transport.hpp>

namespace nano
{
namespace transport
{
	class channel_udp : public nano::transport::channel
	{
	public:
		channel_udp (nano::node &, nano::endpoint const &);
		size_t hash_code () const override;
		bool operator== (nano::transport::channel const &) const override;
		void send_buffer_raw (boost::asio::const_buffer, std::function<void(boost::system::error_code const &, size_t)> const &) const override;
		std::function<void(boost::system::error_code const &, size_t)> callback (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const override;
		std::string to_string () const override;
		bool operator== (nano::transport::channel_udp const & other_a) const
		{
			return &node == &other_a.node && endpoint == other_a.endpoint;
		}
		nano::node & node;
		nano::endpoint endpoint;
	};
	class udp_channels
	{
	public:
		void add (nano::endpoint const &, std::shared_ptr<nano::transport::channel_udp>);
		void erase (nano::endpoint const &);
		size_t size () const;
		std::shared_ptr<nano::transport::channel_udp> channel (nano::endpoint const &) const;

	private:
		mutable std::mutex mutex;
		std::unordered_map<nano::endpoint, std::shared_ptr<nano::transport::channel_udp>> channels;
	};
} // namespace transport
} // namespace nano
