#include <nano/node/node.hpp>
#include <nano/node/scheduler/buckets.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/hinted.hpp>
#include <nano/node/scheduler/optimistic.hpp>

nano::scheduler::component::component (nano::node & node) :
	optimistic_impl{ std::make_unique<nano::scheduler::optimistic> (node.config.optimistic_scheduler, node, node.ledger, node.active, node.network_params.network, node.stats) },
	buckets_impl{ std::make_unique<nano::scheduler::buckets> (node, node.stats) },
	hinted_impl{ std::make_unique<nano::scheduler::hinted> (nano::scheduler::hinted::config{ node.config }, node, node.inactive_vote_cache, node.active, node.online_reps, node.stats) },
	buckets{ *buckets_impl },
	hinted{ *hinted_impl },
	optimistic{ *optimistic_impl }
{
}
