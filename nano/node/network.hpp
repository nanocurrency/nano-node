#pragma once

#include <nano/boost/asio.hpp>
#include <nano/node/common.hpp>
#include <nano/node/transport/tcp.hpp>
#include <nano/node/transport/udp.hpp>

#include <boost/thread/thread.hpp>

#include <memory>
#include <queue>

namespace nano
{
class channel;
class node;
class stats;
class transaction;
class message_buffer final
{
public:
	uint8_t * buffer{ nullptr };
	size_t size{ 0 };
	nano::endpoint endpoint;
};
/**
  * A circular buffer for servicing nano realtime messages.
  * This container follows a producer/consumer model where the operating system is producing data in to
  * buffers which are serviced by internal threads.
  * If buffers are not serviced fast enough they're internally dropped.
  * This container has a maximum space to hold N buffers of M size and will allocate them in round-robin order.
  * All public methods are thread-safe
*/
class message_buffer_manager final
{
public:
	// Stats - Statistics
	// Size - Size of each individual buffer
	// Count - Number of buffers to allocate
	message_buffer_manager (nano::stat & stats, size_t, size_t);
	// Return a buffer where message data can be put
	// Method will attempt to return the first free buffer
	// If there are no free buffers, an unserviced buffer will be dequeued and returned
	// Function will block if there are no free or unserviced buffers
	// Return nullptr if the container has stopped
	nano::message_buffer * allocate ();
	// Queue a buffer that has been filled with message data and notify servicing threads
	void enqueue (nano::message_buffer *);
	// Return a buffer that has been filled with message data
	// Function will block until a buffer has been added
	// Return nullptr if the container has stopped
	nano::message_buffer * dequeue ();
	// Return a buffer to the freelist after is has been serviced
	void release (nano::message_buffer *);
	// Stop container and notify waiting threads
	void stop ();

private:
	nano::stat & stats;
	std::mutex mutex;
	nano::condition_variable condition;
	boost::circular_buffer<nano::message_buffer *> free;
	boost::circular_buffer<nano::message_buffer *> full;
	std::vector<uint8_t> slab;
	std::vector<nano::message_buffer> entries;
	bool stopped;
};
/**
  * Node ID cookies for node ID handshakes
*/
class syn_cookies final
{
public:
	void purge (std::chrono::steady_clock::time_point const &);
	// Returns boost::none if the IP is rate capped on syn cookie requests,
	// or if the endpoint already has a syn cookie query
	boost::optional<nano::uint256_union> assign (nano::endpoint const &);
	// Returns false if valid, true if invalid (true on error convention)
	// Also removes the syn cookie from the store if valid
	bool validate (nano::endpoint const &, nano::account const &, nano::signature const &);
	std::unique_ptr<seq_con_info_component> collect_seq_con_info (std::string const &);

private:
	class syn_cookie_info final
	{
	public:
		nano::uint256_union cookie;
		std::chrono::steady_clock::time_point created_at;
	};
	mutable std::mutex syn_cookie_mutex;
	std::unordered_map<nano::endpoint, syn_cookie_info> cookies;
	std::unordered_map<boost::asio::ip::address, unsigned> cookies_per_ip;
};
class network final
{
public:
	network (nano::node &, uint16_t);
	~network ();
	void start ();
	void stop ();
	void flood_message (nano::message const &, bool const = true);
	void flood_keepalive ()
	{
		nano::keepalive message;
		random_fill (message.peers);
		flood_message (message);
	}
	void flood_vote (std::shared_ptr<nano::vote> vote_a)
	{
		nano::confirm_ack message (vote_a);
		flood_message (message);
	}
	void flood_block (std::shared_ptr<nano::block> block_a, bool const is_droppable_a = true)
	{
		nano::publish publish (block_a);
		flood_message (publish, is_droppable_a);
	}

	void flood_block_batch (std::deque<std::shared_ptr<nano::block>>, unsigned = broadcast_interval_ms);
	void merge_peers (std::array<nano::endpoint, 8> const &);
	void merge_peer (nano::endpoint const &);
	void send_keepalive (std::shared_ptr<nano::transport::channel>);
	void send_keepalive_self (std::shared_ptr<nano::transport::channel>);
	void send_node_id_handshake (std::shared_ptr<nano::transport::channel>, boost::optional<nano::uint256_union> const & query, boost::optional<nano::uint256_union> const & respond_to);
	void send_confirm_req (std::shared_ptr<nano::transport::channel>, std::shared_ptr<nano::block>);
	void broadcast_confirm_req (std::shared_ptr<nano::block>);
	void broadcast_confirm_req_base (std::shared_ptr<nano::block>, std::shared_ptr<std::vector<std::shared_ptr<nano::transport::channel>>>, unsigned, bool = false);
	void broadcast_confirm_req_batch (std::unordered_map<std::shared_ptr<nano::transport::channel>, std::deque<std::pair<nano::block_hash, nano::block_hash>>>, unsigned = broadcast_interval_ms, bool = false);
	void broadcast_confirm_req_batch (std::deque<std::pair<std::shared_ptr<nano::block>, std::shared_ptr<std::vector<std::shared_ptr<nano::transport::channel>>>>>, unsigned = broadcast_interval_ms);
	void confirm_hashes (nano::transaction const &, std::shared_ptr<nano::transport::channel>, std::vector<nano::block_hash>);
	bool send_votes_cache (std::shared_ptr<nano::transport::channel>, nano::block_hash const &);
	std::shared_ptr<nano::transport::channel> find_node_id (nano::account const &);
	std::shared_ptr<nano::transport::channel> find_channel (nano::endpoint const &);
	void process_message (nano::message const &, std::shared_ptr<nano::transport::channel>);
	bool not_a_peer (nano::endpoint const &, bool);
	// Should we reach out to this endpoint with a keepalive message
	bool reachout (nano::endpoint const &, bool = false);
	std::deque<std::shared_ptr<nano::transport::channel>> list (size_t);
	// A list of random peers sized for the configured rebroadcast fanout
	std::deque<std::shared_ptr<nano::transport::channel>> list_fanout ();
	void random_fill (std::array<nano::endpoint, 8> &) const;
	std::unordered_set<std::shared_ptr<nano::transport::channel>> random_set (size_t) const;
	// Get the next peer for attempting a tcp bootstrap connection
	nano::tcp_endpoint bootstrap_peer (bool = false);
	nano::endpoint endpoint ();
	void cleanup (std::chrono::steady_clock::time_point const &);
	void ongoing_cleanup ();
	// Node ID cookies cleanup
	nano::syn_cookies syn_cookies;
	void ongoing_syn_cookie_cleanup ();
	void ongoing_keepalive ();
	size_t size () const;
	size_t size_sqrt () const;
	bool empty () const;
	nano::message_buffer_manager buffer_container;
	boost::asio::ip::udp::resolver resolver;
	std::vector<boost::thread> packet_processing_threads;
	nano::node & node;
	nano::transport::udp_channels udp_channels;
	nano::transport::tcp_channels tcp_channels;
	std::function<void()> disconnect_observer;
	// Called when a new channel is observed
	std::function<void(std::shared_ptr<nano::transport::channel>)> channel_observer;
	static unsigned const broadcast_interval_ms = 10;
	static size_t const buffer_size = 512;
	static size_t const confirm_req_hashes_max = 7;
};
}
