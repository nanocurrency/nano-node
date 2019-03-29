#pragma once

#include <nano/node/common.hpp>
#include <nano/node/stats.hpp>
#include <nano/node/transport/transport.hpp>

namespace nano
{
class message_buffer;
namespace transport
{
	class udp_channels;
	class channel_udp final : public nano::transport::channel
	{
		friend class nano::transport::udp_channels;

	public:
		channel_udp (nano::transport::udp_channels &, nano::endpoint const &, unsigned = nano::protocol_version);
		size_t hash_code () const override;
		bool operator== (nano::transport::channel const &) const override;
		void send_buffer_raw (boost::asio::const_buffer, std::function<void(boost::system::error_code const &, size_t)> const &) const override;
		std::function<void(boost::system::error_code const &, size_t)> callback (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const override;
		std::string to_string () const override;
		bool operator== (nano::transport::channel_udp const & other_a) const
		{
			return &channels == &other_a.channels && endpoint == other_a.endpoint;
		}
		nano::endpoint endpoint;
		std::chrono::steady_clock::time_point last_tcp_attempt{ std::chrono::steady_clock::time_point () };
		std::chrono::steady_clock::time_point last_packet_received{ std::chrono::steady_clock::time_point () };
		unsigned network_version{ nano::protocol_version };
		boost::optional<nano::account> node_id{ boost::none };

	private:
		nano::transport::udp_channels & channels;
	};
	class udp_channels final
	{
		friend class nano::transport::channel_udp;

	public:
		udp_channels (nano::node &, uint16_t);
		std::shared_ptr<nano::transport::channel_udp> insert (nano::endpoint const &, unsigned);
		void erase (nano::endpoint const &);
		size_t size () const;
		std::shared_ptr<nano::transport::channel_udp> channel (nano::endpoint const &) const;
		void random_fill (std::array<nano::endpoint, 8> &) const;
		std::unordered_set<std::shared_ptr<nano::transport::channel_udp>> random_set (size_t) const;
		void store_all (nano::node &);
		bool reserved_address (nano::endpoint const &, bool = false);
		// Get the next peer for attempting a tcp connection
		nano::endpoint tcp_peer ();
		void receive ();
		void start ();
		void stop ();
		void send (boost::asio::const_buffer buffer_a, nano::endpoint endpoint_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a);
		nano::endpoint local_endpoint () const;
		void receive_action (nano::message_buffer *);
		void process_packets ();
		std::shared_ptr<nano::transport::channel> create (nano::endpoint const &);
		// Unassigned, reserved, self
		bool not_a_peer (nano::endpoint const &, bool);
		bool max_ip_connections (nano::endpoint const &);
		// Should we reach out to this endpoint with a keepalive message
		bool reachout (nano::endpoint const &, bool = false);
		std::unique_ptr<seq_con_info_component> collect_seq_con_info (std::string const &);
		void purge (std::chrono::steady_clock::time_point const &);
		void purge_syn_cookies (std::chrono::steady_clock::time_point const &);
		// Returns boost::none if the IP is rate capped on syn cookie requests,
		// or if the endpoint already has a syn cookie query
		boost::optional<nano::uint256_union> assign_syn_cookie (nano::endpoint const &);
		// Returns false if valid, true if invalid (true on error convention)
		// Also removes the syn cookie from the store if valid
		bool validate_syn_cookie (nano::endpoint const &, nano::account const &, nano::signature const &);
		void ongoing_keepalive ();
		std::deque<std::shared_ptr<nano::transport::channel_udp>> list (size_t);
		// A list of random peers sized for the configured rebroadcast fanout
		std::deque<std::shared_ptr<nano::transport::channel_udp>> list_fanout ();
		void modify (std::shared_ptr<nano::transport::channel_udp>);
		// Maximum number of peers per IP
		static size_t constexpr max_peers_per_ip = 10;
		static std::chrono::seconds constexpr syn_cookie_cutoff = std::chrono::seconds (5);

	private:
		bool is_socket_open ();
		void ongoing_syn_cookie_cleanup ();
		class endpoint_tag
		{
		};
		class ip_address_tag
		{
		};
		class random_access_tag
		{
		};
		class last_packet_received_tag
		{
		};
		class last_tcp_attempt_tag
		{
		};
		class channel_udp_wrapper final
		{
		public:
			std::shared_ptr<nano::transport::channel_udp> channel;
			nano::endpoint endpoint () const
			{
				return channel->endpoint;
			}
			std::chrono::steady_clock::time_point last_packet_received () const
			{
				return channel->last_packet_received;
			}
			std::chrono::steady_clock::time_point last_tcp_attempt () const
			{
				return channel->last_tcp_attempt;
			}
			boost::asio::ip::address ip_address () const
			{
				return endpoint ().address ();
			}
		};
		class endpoint_attempt final
		{
		public:
			nano::endpoint endpoint;
			std::chrono::steady_clock::time_point last_attempt;
		};
		class syn_cookie_info final
		{
		public:
			nano::uint256_union cookie;
			std::chrono::steady_clock::time_point created_at;
		};
		mutable std::mutex mutex;
		boost::multi_index_container<
		channel_udp_wrapper,
		boost::multi_index::indexed_by<
		boost::multi_index::random_access<boost::multi_index::tag<random_access_tag>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<last_tcp_attempt_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_tcp_attempt>>,
		boost::multi_index::hashed_unique<boost::multi_index::tag<endpoint_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, nano::endpoint, &channel_udp_wrapper::endpoint>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<last_packet_received_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_packet_received>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<ip_address_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, boost::asio::ip::address, &channel_udp_wrapper::ip_address>>>>
		channels;
		boost::multi_index_container<
		endpoint_attempt,
		boost::multi_index::indexed_by<
		boost::multi_index::hashed_unique<boost::multi_index::member<endpoint_attempt, nano::endpoint, &endpoint_attempt::endpoint>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::member<endpoint_attempt, std::chrono::steady_clock::time_point, &endpoint_attempt::last_attempt>>>>
		attempts;
		std::unordered_map<nano::endpoint, syn_cookie_info> syn_cookies;
		std::unordered_map<boost::asio::ip::address, unsigned> syn_cookies_per_ip;
		nano::node & node;
		boost::asio::ip::udp::socket socket;
		nano::network_params network_params;
	};
} // namespace transport
} // namespace nano
