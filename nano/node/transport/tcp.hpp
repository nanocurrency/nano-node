#pragma once

#include <nano/node/common.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <random>
#include <thread>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace nano
{
class tcp_message_item final
{
public:
	std::shared_ptr<nano::message> message;
	nano::tcp_endpoint endpoint;
	nano::account node_id;
	std::shared_ptr<nano::transport::socket> socket;
};

class tcp_message_manager final
{
public:
	tcp_message_manager (unsigned incoming_connections_max_a);
	void put_message (nano::tcp_message_item const & item_a);
	nano::tcp_message_item get_message ();
	// Stop container and notify waiting threads
	void stop ();

private:
	nano::mutex mutex;
	nano::condition_variable producer_condition;
	nano::condition_variable consumer_condition;
	std::deque<nano::tcp_message_item> entries;
	unsigned max_entries;
	static unsigned const max_entries_per_connection = 16;
	bool stopped{ false };

	friend class network_tcp_message_manager_Test;
};

namespace transport
{
	class tcp_server;
	class tcp_channels;

	class channel_tcp : public nano::transport::channel
	{
		friend class nano::transport::tcp_channels;

	public:
		channel_tcp (nano::node &, std::weak_ptr<nano::transport::socket>);
		~channel_tcp () override;

		std::size_t hash_code () const override;
		bool operator== (nano::transport::channel const &) const override;

		// TODO: investigate clang-tidy warning about default parameters on virtual/override functions//
		void send_buffer (nano::shared_const_buffer const &, std::function<void (boost::system::error_code const &, std::size_t)> const & = nullptr, nano::transport::buffer_drop_policy = nano::transport::buffer_drop_policy::limiter, nano::transport::traffic_type = nano::transport::traffic_type::generic) override;

		std::string to_string () const override;
		bool operator== (nano::transport::channel_tcp const & other_a) const
		{
			return &node == &other_a.node && socket.lock () == other_a.socket.lock ();
		}
		std::weak_ptr<nano::transport::socket> socket;
		/* Mark for temporary channels. Usually remote ports of these channels are ephemeral and received from incoming connections to server.
		If remote part has open listening port, temporary channel will be replaced with direct connection to listening port soon. But if other side is behing NAT or firewall this connection can be pemanent. */
		std::atomic<bool> temporary{ false };

		void set_endpoint ();

		nano::endpoint get_endpoint () const override
		{
			return nano::transport::map_tcp_to_endpoint (get_tcp_endpoint ());
		}

		nano::tcp_endpoint get_tcp_endpoint () const override
		{
			nano::lock_guard<nano::mutex> lk (channel_mutex);
			return endpoint;
		}

		nano::transport::transport_type get_type () const override
		{
			return nano::transport::transport_type::tcp;
		}

		virtual bool max (nano::transport::traffic_type traffic_type) override
		{
			bool result = true;
			if (auto socket_l = socket.lock ())
			{
				result = socket_l->max (traffic_type);
			}
			return result;
		}

		virtual bool alive () const override
		{
			if (auto socket_l = socket.lock ())
			{
				return socket_l->alive ();
			}
			return false;
		}

	private:
		nano::tcp_endpoint endpoint{ boost::asio::ip::address_v6::any (), 0 };

	public: // Logging
		void operator() (nano::object_stream &) const override;
	};

	class tcp_channels final
	{
		friend class nano::transport::channel_tcp;
		friend class telemetry_simultaneous_requests_Test;
		friend class network_peer_max_tcp_attempts_subnetwork_Test;

	public:
		explicit tcp_channels (nano::node &, std::function<void (nano::message const &, std::shared_ptr<nano::transport::channel> const &)> sink = nullptr);
		~tcp_channels ();

		void start ();
		void stop ();

		bool insert (std::shared_ptr<nano::transport::channel_tcp> const &, std::shared_ptr<nano::transport::socket> const &, std::shared_ptr<nano::transport::tcp_server> const &);
		void erase (nano::tcp_endpoint const &);
		std::size_t size () const;
		std::shared_ptr<nano::transport::channel_tcp> find_channel (nano::tcp_endpoint const &) const;
		void random_fill (std::array<nano::endpoint, 8> &) const;
		std::unordered_set<std::shared_ptr<nano::transport::channel>> random_set (std::size_t, uint8_t = 0, bool = false) const;
		bool store_all (bool = true);
		std::shared_ptr<nano::transport::channel_tcp> find_node_id (nano::account const &);
		// Get the next peer for attempting a tcp connection
		nano::tcp_endpoint bootstrap_peer ();
		void process_messages ();
		void process_message (nano::message const &, nano::tcp_endpoint const &, nano::account const &, std::shared_ptr<nano::transport::socket> const &);
		bool max_ip_connections (nano::tcp_endpoint const & endpoint_a);
		bool max_subnetwork_connections (nano::tcp_endpoint const & endpoint_a);
		bool max_ip_or_subnetwork_connections (nano::tcp_endpoint const & endpoint_a);
		// Should we reach out to this endpoint with a keepalive message? If yes, register a new reachout attempt
		bool track_reachout (nano::endpoint const &);
		std::unique_ptr<container_info_component> collect_container_info (std::string const &);
		void purge (std::chrono::steady_clock::time_point const &);
		void list (std::deque<std::shared_ptr<nano::transport::channel>> &, uint8_t = 0, bool = true);
		void modify (std::shared_ptr<nano::transport::channel_tcp> const &, std::function<void (std::shared_ptr<nano::transport::channel_tcp> const &)>);
		void update (nano::tcp_endpoint const &);
		void keepalive ();
		std::optional<nano::keepalive> sample_keepalive ();

		// Connection start
		void start_tcp (nano::endpoint const &);
		void start_tcp_receive_node_id (std::shared_ptr<nano::transport::channel_tcp> const &, nano::endpoint const &, std::shared_ptr<std::vector<uint8_t>> const &);

	private: // Dependencies
		nano::node & node;

	public:
		nano::tcp_message_manager message_manager;

	private:
		class channel_entry final
		{
		public:
			std::shared_ptr<nano::transport::channel_tcp> channel;
			std::shared_ptr<nano::transport::socket> socket;
			std::shared_ptr<nano::transport::tcp_server> response_server;

		public:
			channel_entry (std::shared_ptr<nano::transport::channel_tcp> channel_a, std::shared_ptr<nano::transport::socket> socket_a, std::shared_ptr<nano::transport::tcp_server> server_a) :
				channel (std::move (channel_a)), socket (std::move (socket_a)), response_server (std::move (server_a))
			{
			}
			nano::tcp_endpoint endpoint () const
			{
				return channel->get_tcp_endpoint ();
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
				return nano::transport::ipv4_address_or_ipv6_subnet (endpoint ().address ());
			}
			boost::asio::ip::address subnetwork () const
			{
				return nano::transport::map_address_to_subnetwork (endpoint ().address ());
			}
			nano::account node_id () const
			{
				auto node_id (channel->get_node_id ());
				return node_id;
			}
			uint8_t network_version () const
			{
				return channel->get_network_version ();
			}
		};

		class attempt_entry final
		{
		public:
			nano::tcp_endpoint endpoint;
			boost::asio::ip::address address;
			boost::asio::ip::address subnetwork;
			std::chrono::steady_clock::time_point last_attempt{ std::chrono::steady_clock::now () };

		public:
			explicit attempt_entry (nano::tcp_endpoint const & endpoint_a) :
				endpoint (endpoint_a),
				address (nano::transport::ipv4_address_or_ipv6_subnet (endpoint_a.address ())),
				subnetwork (nano::transport::map_address_to_subnetwork (endpoint_a.address ()))
			{
			}
		};

		// clang-format off
		class endpoint_tag {};
		class ip_address_tag {};
		class subnetwork_tag {};
		class random_access_tag {};
		class last_packet_sent_tag {};
		class last_bootstrap_attempt_tag {};
		class last_attempt_tag {};
		class node_id_tag {};
		class version_tag {};
		// clang-format on

		// clang-format off
		boost::multi_index_container<channel_entry,
		mi::indexed_by<
			mi::random_access<mi::tag<random_access_tag>>,
			mi::ordered_non_unique<mi::tag<last_bootstrap_attempt_tag>,
				mi::const_mem_fun<channel_entry, std::chrono::steady_clock::time_point, &channel_entry::last_bootstrap_attempt>>,
			mi::hashed_unique<mi::tag<endpoint_tag>,
				mi::const_mem_fun<channel_entry, nano::tcp_endpoint, &channel_entry::endpoint>>,
			mi::hashed_non_unique<mi::tag<node_id_tag>,
				mi::const_mem_fun<channel_entry, nano::account, &channel_entry::node_id>>,
			mi::ordered_non_unique<mi::tag<last_packet_sent_tag>,
				mi::const_mem_fun<channel_entry, std::chrono::steady_clock::time_point, &channel_entry::last_packet_sent>>,
			mi::ordered_non_unique<mi::tag<version_tag>,
				mi::const_mem_fun<channel_entry, uint8_t, &channel_entry::network_version>>,
			mi::hashed_non_unique<mi::tag<ip_address_tag>,
				mi::const_mem_fun<channel_entry, boost::asio::ip::address, &channel_entry::ip_address>>,
			mi::hashed_non_unique<mi::tag<subnetwork_tag>,
				mi::const_mem_fun<channel_entry, boost::asio::ip::address, &channel_entry::subnetwork>>>>
		channels;

		boost::multi_index_container<attempt_entry,
		mi::indexed_by<
			mi::hashed_unique<mi::tag<endpoint_tag>,
				mi::member<attempt_entry, nano::tcp_endpoint, &attempt_entry::endpoint>>,
			mi::hashed_non_unique<mi::tag<ip_address_tag>,
				mi::member<attempt_entry, boost::asio::ip::address, &attempt_entry::address>>,
			mi::hashed_non_unique<mi::tag<subnetwork_tag>,
				mi::member<attempt_entry, boost::asio::ip::address, &attempt_entry::subnetwork>>,
			mi::ordered_non_unique<mi::tag<last_attempt_tag>,
				mi::member<attempt_entry, std::chrono::steady_clock::time_point, &attempt_entry::last_attempt>>>>
		attempts;
		// clang-format on

	private:
		std::function<void (nano::message const &, std::shared_ptr<nano::transport::channel> const &)> sink;

		std::atomic<bool> stopped{ false };
		nano::condition_variable condition;
		mutable nano::mutex mutex;

		std::default_random_engine rng;
	};
} // namespace transport
} // namespace nano
