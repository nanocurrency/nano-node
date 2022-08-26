#include <nano/node/node_observers.hpp>

std::unique_ptr<nano::container_info_component> nano::collect_container_info (nano::node_observers & node_observers, std::string const & name)
{
	auto composite = std::make_unique<nano::container_info_composite> (name);
	composite->add_component (node_observers.blocks.collect_container_info ("blocks"));
	composite->add_component (node_observers.wallet.collect_container_info ("wallet"));
	composite->add_component (node_observers.vote.collect_container_info ("vote"));
	composite->add_component (node_observers.active_started.collect_container_info ("active_started"));
	composite->add_component (node_observers.active_stopped.collect_container_info ("active_stopped"));
	composite->add_component (node_observers.account_balance.collect_container_info ("account_balance"));
	composite->add_component (node_observers.endpoint.collect_container_info ("endpoint"));
	composite->add_component (node_observers.disconnect.collect_container_info ("disconnect"));
	composite->add_component (node_observers.work_cancel.collect_container_info ("work_cancel"));
	return composite;
}
