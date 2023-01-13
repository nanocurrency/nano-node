#pragma once

#include <miniupnp/miniupnpc/include/miniupnpc.h>

namespace nano
{
class node;

/** Collected protocol information */
class mapping_protocol
{
public:
	/** Protocol name; TPC or UDP */
	char const * name;
	boost::asio::ip::address_v4 external_address;
	uint16_t external_port;
	bool enabled;
	std::string to_string ();
};

/** Collection of discovered UPnP devices and state*/
class upnp_state
{
public:
	upnp_state () = default;
	~upnp_state ();
	upnp_state & operator= (upnp_state &&);
	std::string to_string ();

	/** List of discovered UPnP devices */
	UPNPDev * devices{ nullptr };
	/** UPnP collected url information */
	UPNPUrls urls{ 0 };
	/** UPnP state */
	IGDdatas data{ { 0 } };
};

/** UPnP port mapping */
class port_mapping
{
public:
	port_mapping (nano::node &);
	void start ();
	void stop ();
	void refresh_devices ();
	nano::endpoint external_address ();
	std::string to_string ();

private:
	/** Add port mappings for the node port (not RPC). Refresh when the lease ends. */
	void refresh_mapping ();
	/** Check occasionally to refresh in case router loses mapping */
	void check_mapping_loop ();
	/** Returns false if mapping still exists */
	bool check_lost_or_old_mapping ();
	std::string get_config_port (std::string const &);
	upnp_state upnp;
	nano::node & node;
	boost::asio::ip::address_v4 address;
	std::array<mapping_protocol, 2> protocols;
	uint64_t check_count{ 0 };
	std::atomic<bool> on{ false };
	nano::mutex mutex;
};
}
