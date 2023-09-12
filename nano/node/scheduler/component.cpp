#include <nano/node/node.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/hinted.hpp>
#include <nano/node/scheduler/optimistic.hpp>
#include <nano/node/scheduler/priority.hpp>

nano::scheduler::component::component (nano::node & node) :
	hinted_impl{ std::make_unique<nano::scheduler::hinted> (nano::scheduler::hinted::config{ node.config }, node, node.inactive_vote_cache, node.active, node.online_reps, node.stats) },
	optimistic_impl{ std::make_unique<nano::scheduler::optimistic> (node.config.optimistic_scheduler, node, node.ledger, node.active, node.network_params.network, node.stats) },
	priority_impl{ std::make_unique<nano::scheduler::priority> (node, node.stats) },
	hinted{ *hinted_impl },
	optimistic{ *optimistic_impl },
	priority{ *priority_impl }
{
}

nano::scheduler::component::~component ()
{
}

void nano::scheduler::component::start ()
{
	hinted.start ();
	optimistic.start ();
	priority.start ();
}

void nano::scheduler::component::stop ()
{
	hinted.stop ();
	optimistic.stop ();
	priority.stop ();
}

std::unique_ptr<nano::container_info_component> nano::scheduler::component::collect_container_info (std::string const & name)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	//composite->add_component (hinted.collect_container_info ("hinted"));
	//composite->add_component (optimistic.collect_container_info ("optimistic"));
	composite->add_component (priority.collect_container_info ("priority"));
	return composite;
}
