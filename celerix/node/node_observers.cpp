#include <celerix/node/node_observers.hpp>

celerix::container_info celerix::node_observers::container_info () const
{
	celerix::container_info info;
	info.put ("blocks", blocks.size ());
	info.put ("wallet", wallet.size ());
	info.put ("vote", vote.size ());
	info.put ("active_started", active_started.size ());
	info.put ("active_stopped", active_stopped.size ());
	info.put ("account_balance", account_balance.size ());
	info.put ("disconnect", disconnect.size ());
	info.put ("work_cancel", work_cancel.size ());
	info.put ("telemetry", telemetry.size ());
	info.put ("socket_connected", socket_connected.size ());
	info.put ("channel_connected", channel_connected.size ());
	return info;
}
