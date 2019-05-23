#pragma once

#include <nano/lib/config.hpp>

#include <boost/asio/ip/address_v4.hpp>

#include <mutex>

#include <miniupnp/miniupnpc/miniupnpc.h>

namespace nano
{
class node;

/** Collected protocol information */
class mapping_protocol
{
public:
	/** Protocol name; TPC or UDP */
	char const * name;
	int remaining;
	boost::asio::ip::address_v4 external_address;
	uint16_t external_port;
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

private:
	/** Add port mappings for the node port (not RPC). Refresh when the lease ends. */
	void refresh_mapping ();
	/** Refresh occasionally in case router loses mapping */
	void check_mapping_loop ();
	int check_mapping ();
	std::mutex mutex;
	nano::node & node;
	/** List of all UPnP devices */
	UPNPDev * devices;
	/** UPnP collected url information */
	UPNPUrls urls;
	/** UPnP state */
	IGDdatas data;
	nano::network_params network_params;
	boost::asio::ip::address_v4 address;
	std::array<mapping_protocol, 2> protocols;
	uint64_t check_count;
	bool on;
};
}
