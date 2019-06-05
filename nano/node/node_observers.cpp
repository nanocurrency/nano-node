#include <nano/node/node_observers.hpp>

std::unique_ptr<nano::seq_con_info_component> nano::collect_seq_con_info (nano::node_observers & node_observers, const std::string & name)
{
	auto composite = std::make_unique<nano::seq_con_info_composite> (name);
	composite->add_component (collect_seq_con_info (node_observers.blocks, "blocks"));
	composite->add_component (collect_seq_con_info (node_observers.wallet, "wallet"));
	composite->add_component (collect_seq_con_info (node_observers.vote, "vote"));
	composite->add_component (collect_seq_con_info (node_observers.active_stopped, "active_stopped"));
	composite->add_component (collect_seq_con_info (node_observers.account_balance, "account_balance"));
	composite->add_component (collect_seq_con_info (node_observers.endpoint, "endpoint"));
	composite->add_component (collect_seq_con_info (node_observers.disconnect, "disconnect"));
	return composite;
}
