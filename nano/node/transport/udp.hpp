#pragma once

#include <mutex>
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
		~channel_udp ();
		size_t hash_code () const override;
		bool operator== (nano::transport::channel const &) const override;
		void send_buffer (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) override;
		std::function<void(boost::system::error_code const &, size_t)> callback (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const override;
		std::string to_string () const override;
		bool operator== (nano::transport::channel_udp const & other_a) const
		{
			return &channels == &other_a.channels && endpoint == other_a.endpoint;
		}

		nano::endpoint get_endpoint () const override
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			return endpoint;
		}

		nano::tcp_endpoint get_tcp_endpoint () const override
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			return nano::transport::map_endpoint_to_tcp (endpoint);
		}

	private:
		nano::endpoint endpoint;
		nano::transport::udp_channels & channels;
		std::shared_ptr<nano::socket> socket;
	};
	class udp_channels final
	{
		friend class nano::transport::channel_udp;

	public:
		udp_channels (nano::node &, uint16_t);
		std::shared_ptr<nano::transport::channel_udp> insert (nano::endpoint const &, unsigned);
		void erase (nano::endpoint const &);
		void insert_tcp (std::shared_ptr<nano::transport::channel_udp>);
		size_t size () const;
		std::shared_ptr<nano::transport::channel_udp> channel (nano::endpoint const &) const;
		void random_fill (std::array<nano::endpoint, 8> &) const;
		std::unordered_set<std::shared_ptr<nano::transport::channel_udp>> random_set (size_t) const;
		void store_all (nano::node &);
		nano::endpoint find_node_id (nano::account const &);
		void clean_node_id (nano::endpoint const &, nano::account const &);
		// Get the next peer for attempting a tcp connection
		nano::endpoint tcp_peer ();
		void receive ();
		void start ();
		void stop ();
		void send (boost::asio::const_buffer buffer_a, nano::endpoint endpoint_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a);
		void start_tcp (std::shared_ptr<nano::transport::channel_udp>, std::function<void()> const & = nullptr);
		void start_tcp_receive_header (std::shared_ptr<nano::transport::channel_udp>, std::shared_ptr<std::vector<uint8_t>>, std::function<void()> const &);
		void start_tcp_receive (std::shared_ptr<nano::transport::channel_udp>, std::shared_ptr<std::vector<uint8_t>>, std::function<void()> const &);
		nano::endpoint get_local_endpoint () const;
		void receive_action (nano::message_buffer *);
		void process_packets ();
		std::shared_ptr<nano::transport::channel> create (nano::endpoint const &);
		bool max_ip_connections (nano::endpoint const &);
		// Should we reach out to this endpoint with a keepalive message
		bool reachout (nano::endpoint const &);
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
		// Common messages
		void common_keepalive (nano::keepalive const &, nano::endpoint const &, nano::transport::transport_type = nano::transport::transport_type::udp, bool = false);
		// Response channels
		void add_response_channels (nano::tcp_endpoint const &, std::vector<nano::endpoint>);
		std::shared_ptr<nano::transport::channel_udp> search_response_channel (nano::tcp_endpoint const &);
		void remove_response_channel (nano::tcp_endpoint const &);
		size_t response_channels_size () const;
		// Maximum number of peers per IP
		static size_t constexpr max_peers_per_ip = 10;
		static std::chrono::seconds constexpr syn_cookie_cutoff = std::chrono::seconds (5);
		nano::node & node;

	private:
		void close_socket ();
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
		class last_packet_sent_tag
		{
		};
		class last_bootstrap_attempt_tag
		{
		};
		class node_id_tag
		{
		};
		class channel_udp_wrapper final
		{
		public:
			std::shared_ptr<nano::transport::channel_udp> channel;
			nano::endpoint endpoint () const
			{
				return channel->get_endpoint ();
			}
			std::chrono::steady_clock::time_point last_packet_received () const
			{
				return channel->get_last_packet_received ();
			}
			std::chrono::steady_clock::time_point last_packet_sent () const
			{
				return channel->get_last_packet_sent ();
			}
			std::chrono::steady_clock::time_point last_bootstrap_attempt () const
			{
				return channel->get_last_bootstrap_attempt ();
			}
			boost::asio::ip::address ip_address () const
			{
				return endpoint ().address ();
			}
			nano::account node_id () const
			{
				auto node_id_l (channel->get_node_id ());
				if (node_id_l.is_initialized ())
				{
					return node_id_l.get ();
				}
				else
				{
					return 0;
				}
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
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<last_bootstrap_attempt_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_bootstrap_attempt>>,
		boost::multi_index::hashed_unique<boost::multi_index::tag<endpoint_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, nano::endpoint, &channel_udp_wrapper::endpoint>>,
		boost::multi_index::hashed_non_unique<boost::multi_index::tag<node_id_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, nano::account, &channel_udp_wrapper::node_id>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<last_packet_received_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_packet_received>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<last_packet_sent_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_packet_sent>>,
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
		std::unordered_map<nano::tcp_endpoint, std::vector<nano::endpoint>> response_channels;
		boost::asio::strand<boost::asio::io_context::executor_type> strand;
		boost::asio::ip::udp::socket socket;
		nano::endpoint local_endpoint;
		nano::network_params network_params;
		std::atomic<bool> stopped{ false };
	};
} // namespace transport
} // namespace nano
