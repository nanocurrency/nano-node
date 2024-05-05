#include <nano/node/node_observers.hpp>

nano::container_info nano::node_observers::container_info () const
{
	nano::container_info info;
	info.put ("blocks", blocks.size ());
	info.put ("wallet", wallet.size ());
	info.put ("vote", vote.size ());
	info.put ("active_started", active_started.size ());
	info.put ("active_stopped", active_stopped.size ());
	info.put ("account_balance", account_balance.size ());
	info.put ("endpoint", endpoint.size ());
	info.put ("disconnect", disconnect.size ());
	info.put ("work_cancel", work_cancel.size ());
	info.put ("telemetry", telemetry.size ());
	info.put ("socket_connected", socket_connected.size ());
	return info;
}
