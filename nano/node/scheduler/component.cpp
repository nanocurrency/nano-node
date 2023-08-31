#include <nano/node/node.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/hinted.hpp>
#include <nano/node/scheduler/optimistic.hpp>
#include <nano/node/scheduler/priority.hpp>

nano::scheduler::component::component (nano::node & node) :
	optimistic_impl{ std::make_unique<nano::scheduler::optimistic> (node.config.optimistic_scheduler, node, node.ledger, node.active, node.network_params.network, node.stats) },
	priority_impl{ std::make_unique<nano::scheduler::priority> (node, node.stats) },
	hinted_impl{ std::make_unique<nano::scheduler::hinted> (nano::scheduler::hinted::config{ node.config }, node, node.inactive_vote_cache, node.active, node.online_reps, node.stats) },
	priority{ *priority_impl },
	hinted{ *hinted_impl },
	optimistic{ *optimistic_impl }
{
}
