#include <nano/lib/thread_roles.hpp>
#include <nano/node/node.hpp>
#include <nano/node/portmapping.hpp>

#include <miniupnp/miniupnpc/include/upnpcommands.h>
#include <miniupnp/miniupnpc/include/upnperrors.h>

#include <boost/range/adaptor/filtered.hpp>

std::string nano::mapping_protocol::to_string ()
{
	std::stringstream ss;
	ss << name << " " << external_address << ":" << external_port;
	ss << (enabled ? " (enabled)" : " (disabled)");
	return ss.str ();
};

/*
 * port_mapping
 */

nano::port_mapping::port_mapping (nano::node & node_a) :
	node (node_a),
	// Kept UDP in the array (set disabled) so the port mapping is still
	// implemented in case other transport protocols that rely on it is added.
	protocols ({ { { "TCP", boost::asio::ip::address_v4::any (), 0, true }, { "UDP", boost::asio::ip::address_v4::any (), 0, false } } })
{
}

nano::port_mapping::~port_mapping ()
{
	debug_assert (!thread.joinable ());
}

void nano::port_mapping::start ()
{
	debug_assert (!thread.joinable ());

	// Long discovery time and fast setup/teardown make this impractical for testing
	// TODO: Find a way to test this
	if (node.network_params.network.is_dev_network ())
	{
		return;
	}

	thread = std::thread ([this] {
		nano::thread_role::set (nano::thread_role::name::port_mapping);
		run ();
	});
}

void nano::port_mapping::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard (mutex);
		stopped = true;
	}
	condition.notify_all ();

	if (thread.joinable ())
	{
		thread.join ();
	}

	nano::lock_guard<nano::mutex> guard_l (mutex);
	for (auto & protocol : protocols | boost::adaptors::filtered ([] (auto const & p) { return p.enabled; }))
	{
		if (protocol.external_port != 0)
		{
			std::string external_port_str = std::to_string (protocol.external_port);
			std::string address_str = address.to_string ();

			// Be a good citizen for the router and shut down our mapping
			auto delete_error_l = UPNP_DeletePortMapping (upnp.urls.controlURL, upnp.data.first.servicetype, external_port_str.c_str (), protocol.name, address_str.c_str ());
			if (delete_error_l)
			{
				node.logger.warn (nano::log::type::upnp, "UPnP shutdown {} port mapping failed: {} ({})",
				protocol.name,
				delete_error_l,
				strupnperror (delete_error_l));
			}
			else
			{
				node.logger.info (nano::log::type::upnp, "UPnP shutdown {} port mapping successful: {}:{}",
				protocol.name,
				protocol.external_address.to_string (),
				protocol.external_port);
			}
		}
	}
}

std::string nano::port_mapping::get_config_port (std::string const & node_port_a)
{
	return node.config.external_port != 0 ? std::to_string (node.config.external_port) : node_port_a;
}

std::string nano::port_mapping::to_string ()
{
	std::stringstream ss;

	ss << "port_mapping is " << (stopped ? "stopped" : "running") << std::endl;
	for (auto & protocol : protocols)
	{
		ss << protocol.to_string () << std::endl;
	}
	ss << upnp.to_string ();

	return ss.str ();
};

void nano::port_mapping::refresh_devices ()
{
	upnp_state upnp_l;
	int discover_error_l = 0;
	upnp_l.devices = upnpDiscover (2000, nullptr, nullptr, UPNP_LOCAL_PORT_ANY, false, 2, &discover_error_l);
	std::array<char, 64> local_address_l;
	local_address_l.fill (0);
	auto igd_error_l (UPNP_GetValidIGD (upnp_l.devices, &upnp_l.urls, &upnp_l.data, local_address_l.data (), sizeof (local_address_l)));

	// Bump logging level periodically
	node.logger.log ((check_count % 15 == 0) ? nano::log::level::info : nano::log::level::debug,
	nano::log::type::upnp, "UPnP local address {}, discovery: {}, IGD search: {}",
	local_address_l.data (),
	discover_error_l,
	igd_error_l);

	for (auto i (upnp_l.devices); i != nullptr; i = i->pNext)
	{
		node.logger.debug (nano::log::type::upnp, "UPnP device url: {}, st: {}, usn: {}", i->descURL, i->st, i->usn);
	}

	// Update port mapping
	nano::lock_guard<nano::mutex> guard_l (mutex);
	upnp = std::move (upnp_l);
	if (igd_error_l == 1 || igd_error_l == 2)
	{
		boost::system::error_code ec;
		address = boost::asio::ip::address_v4::from_string (local_address_l.data (), ec);
	}
}

nano::endpoint nano::port_mapping::external_address ()
{
	nano::endpoint result_l (boost::asio::ip::address_v6{}, 0);
	nano::lock_guard<nano::mutex> guard_l (mutex);
	for (auto & protocol : protocols | boost::adaptors::filtered ([] (auto const & p) { return p.enabled; }))
	{
		if (protocol.external_port != 0)
		{
			result_l = nano::endpoint (protocol.external_address, protocol.external_port);
		}
	}
	return result_l;
}

void nano::port_mapping::refresh_mapping ()
{
	nano::lock_guard<nano::mutex> guard_l (mutex);

	if (stopped)
	{
		return;
	}

	auto node_port_l (std::to_string (node.network.endpoint ().port ()));
	auto config_port_l (get_config_port (node_port_l));

	// We don't map the RPC port because, unless RPC authentication was added, this would almost always be a security risk
	for (auto & protocol : protocols | boost::adaptors::filtered ([] (auto const & p) { return p.enabled; }))
	{
		auto upnp_description = std::string ("Nano Node (") + node.network_params.network.get_current_network_as_string () + ")";
		std::string address_str = address.to_string ();
		std::string lease_duration_str = std::to_string (node.network_params.portmapping.lease_duration.count ());

		auto add_port_mapping_error_l = UPNP_AddPortMapping (upnp.urls.controlURL, upnp.data.first.servicetype, config_port_l.c_str (), node_port_l.c_str (), address_str.c_str (), upnp_description.c_str (), protocol.name, nullptr, lease_duration_str.c_str ());
		if (add_port_mapping_error_l == UPNPCOMMAND_SUCCESS)
		{
			protocol.external_port = static_cast<uint16_t> (std::atoi (config_port_l.data ()));

			node.logger.info (nano::log::type::upnp, "UPnP {} {}:{} mapped to: {}",
			protocol.name,
			protocol.external_address.to_string (),
			config_port_l,
			node_port_l);
		}
		else
		{
			protocol.external_port = 0;

			node.logger.warn (nano::log::type::upnp, "UPnP {} {}:{} failed: {} ({})",
			protocol.name,
			protocol.external_address.to_string (),
			config_port_l,
			add_port_mapping_error_l,
			strupnperror (add_port_mapping_error_l));
		}
	}
}

bool nano::port_mapping::check_lost_or_old_mapping ()
{
	bool result_l (false);
	nano::lock_guard<nano::mutex> guard_l (mutex);
	auto node_port_l (std::to_string (node.network.endpoint ().port ()));
	auto config_port_l (get_config_port (node_port_l));
	for (auto & protocol : protocols | boost::adaptors::filtered ([] (auto const & p) { return p.enabled; }))
	{
		std::array<char, 64> int_client_l;
		std::array<char, 6> int_port_l;
		std::array<char, 16> remaining_mapping_duration_l;
		remaining_mapping_duration_l.fill (0);
		auto verify_port_mapping_error_l (UPNP_GetSpecificPortMappingEntry (upnp.urls.controlURL, upnp.data.first.servicetype, config_port_l.c_str (), protocol.name, nullptr, int_client_l.data (), int_port_l.data (), nullptr, nullptr, remaining_mapping_duration_l.data ()));
		auto remaining_from_port_mapping = std::atoi (remaining_mapping_duration_l.data ());
		auto lease_duration = node.network_params.portmapping.lease_duration.count ();
		auto lease_duration_divided_by_two = (lease_duration / 2);
		auto recent_lease = (remaining_from_port_mapping >= lease_duration_divided_by_two);
		if (verify_port_mapping_error_l != UPNPCOMMAND_SUCCESS)
		{
			result_l = true;

			node.logger.warn (nano::log::type::upnp, "UPnP get specific port mapping failed: {} ({})",
			verify_port_mapping_error_l,
			strupnperror (verify_port_mapping_error_l));
		}
		if (!recent_lease)
		{
			result_l = true;

			node.logger.info (nano::log::type::upnp, "UPnP lease time getting old, remaining time: {}, lease time: {}, below the threshold: {}",
			remaining_from_port_mapping,
			lease_duration,
			lease_duration_divided_by_two);
		}
		std::array<char, 64> external_address_l;
		external_address_l.fill (0);
		auto external_ip_error_l (UPNP_GetExternalIPAddress (upnp.urls.controlURL, upnp.data.first.servicetype, external_address_l.data ()));
		if (external_ip_error_l == UPNPCOMMAND_SUCCESS)
		{
			boost::system::error_code ec;
			protocol.external_address = boost::asio::ip::address_v4::from_string (external_address_l.data (), ec);
			protocol.external_port = static_cast<uint16_t> (std::atoi (config_port_l.data ()));
		}
		else
		{
			protocol.external_address = boost::asio::ip::address_v4::any ();

			node.logger.warn (nano::log::type::upnp, "UPnP get external ip address failed: {} ({})",
			external_ip_error_l,
			strupnperror (external_ip_error_l));
		}

		node.logger.debug (nano::log::type::upnp, "UPnP {} mapping verification response: {}, external ip response: {}, external ip: {}, internal ip: {}, remaining lease: {}",
		protocol.name,
		verify_port_mapping_error_l,
		external_ip_error_l,
		external_address_l.data (),
		address.to_string (),
		remaining_mapping_duration_l.data ());
	}
	return result_l;
}

void nano::port_mapping::check_mapping ()
{
	debug_assert (!node.network_params.network.is_dev_network ());

	refresh_devices ();

	if (upnp.devices != nullptr)
	{
		// If the mapping is lost, refresh it
		if (check_lost_or_old_mapping ())
		{
			// Schedules a mapping refresh just before the leasing ends
			refresh_mapping ();
		}
		else
		{
			node.logger.info (nano::log::type::upnp, "UPnP No need to refresh the mapping");
		}
	}
	else
	{
		// Bump logging level periodically
		node.logger.log ((check_count % 15 == 0) ? nano::log::level::info : nano::log::level::debug,
		nano::log::type::upnp, "UPnP No IGD devices found");
	}

	++check_count;
}

void nano::port_mapping::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		node.stats.inc (nano::stat::type::port_mapping, nano::stat::detail::loop);

		lock.unlock ();
		check_mapping ();
		lock.lock ();

		condition.wait_for (lock, node.network_params.portmapping.health_check_period, [this] { return stopped.load (); });
	}
}

/*
 * upnp_state
 */

std::string nano::upnp_state::to_string ()
{
	std::stringstream ss;
	ss << "Discovered UPnP devices:" << std::endl;
	for (UPNPDev * p = devices; p; p = p->pNext)
	{
		debug_assert (p->descURL);
		debug_assert (p->st);
		debug_assert (p->usn);
		ss << "  " << p->descURL << std::endl;
		ss << "  " << p->st << std::endl;
		ss << "  " << p->usn << std::endl;
	}
	ss << "  scope_id: " << std::endl;
	return ss.str ();
}

nano::upnp_state::~upnp_state ()
{
	if (devices)
	{
		freeUPNPDevlist (devices);
	}
	FreeUPNPUrls (&urls);
}

nano::upnp_state & nano::upnp_state::operator= (nano::upnp_state && other_a)
{
	if (this == &other_a)
	{
		return *this;
	}
	if (devices)
	{
		freeUPNPDevlist (devices);
	}
	devices = other_a.devices;
	other_a.devices = nullptr;
	FreeUPNPUrls (&urls);
	urls = other_a.urls;
	other_a.urls = { 0 };
	data = other_a.data;
	other_a.data = { { 0 } };
	return *this;
}
