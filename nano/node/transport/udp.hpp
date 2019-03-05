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
		channel_udp (nano::node &, nano::endpoint const &, unsigned = nano::protocol_version);
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
		std::chrono::steady_clock::time_point last_tcp_attempt{ std::chrono::steady_clock::time_point () };
		unsigned network_version{ nano::protocol_version };
	};
	class udp_channels
	{
	public:
		void add (std::shared_ptr<nano::transport::channel_udp>);
		void erase (nano::endpoint const &);
		size_t size () const;
		std::shared_ptr<nano::transport::channel_udp> channel (nano::endpoint const &) const;
		void random_fill (std::array<nano::endpoint, 8> &) const;
		std::unordered_set<std::shared_ptr<nano::transport::channel_udp>> random_set (size_t) const;
		void store_all (nano::node &);
		bool reserved_address (nano::endpoint const &);
		// Get the next peer for attempting a tcp connection
		nano::endpoint tcp_peer ();

	private:
		class endpoint_tag
		{
		};
		class random_access_tag
		{
		};
		class last_tcp_attempt_tag
		{
		};
		class channel_udp_wrapper
		{
		public:
			std::shared_ptr<nano::transport::channel_udp> channel;
			nano::endpoint endpoint () const
			{
				return channel->endpoint;
			}
			std::chrono::steady_clock::time_point last_tcp_attempt () const
			{
				return channel->last_tcp_attempt;
			}
		};
		mutable std::mutex mutex;
		boost::multi_index_container<
		channel_udp_wrapper,
		boost::multi_index::indexed_by<
		boost::multi_index::random_access<boost::multi_index::tag<random_access_tag>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<last_tcp_attempt_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_tcp_attempt>>,
		boost::multi_index::hashed_unique<boost::multi_index::tag<endpoint_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, nano::endpoint, &channel_udp_wrapper::endpoint>>>>
		channels;
	};
} // namespace transport
} // namespace nano
