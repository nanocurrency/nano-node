#include <nano/node/node.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/hinted.hpp>
#include <nano/node/scheduler/manual.hpp>
#include <nano/node/scheduler/optimistic.hpp>
#include <nano/node/scheduler/priority.hpp>

nano::scheduler::component::component (nano::node_config & node_config, nano::node & node, nano::ledger & ledger, nano::active_elections & active, nano::online_reps & online_reps, nano::vote_cache & vote_cache, nano::stats & stats, nano::logger & logger) :
	hinted_impl{ std::make_unique<nano::scheduler::hinted> (node_config.hinted_scheduler, node, vote_cache, active, online_reps, stats) },
	manual_impl{ std::make_unique<nano::scheduler::manual> (node) },
	optimistic_impl{ std::make_unique<nano::scheduler::optimistic> (node_config.optimistic_scheduler, node, ledger, active, node_config.network_params.network, stats) },
	priority_impl{ std::make_unique<nano::scheduler::priority> (node_config, node, ledger, active, stats, logger) },
	hinted{ *hinted_impl },
	manual{ *manual_impl },
	optimistic{ *optimistic_impl },
	priority{ *priority_impl }
{
	// Notify election schedulers when AEC frees election slot
	active.vacancy_updated.add ([this] () {
		priority.notify ();
		hinted.notify ();
		optimistic.notify ();
	});
}

nano::scheduler::component::~component ()
{
}

void nano::scheduler::component::start ()
{
	hinted.start ();
	manual.start ();
	optimistic.start ();
	priority.start ();
}

void nano::scheduler::component::stop ()
{
	hinted.stop ();
	manual.stop ();
	optimistic.stop ();
	priority.stop ();
}

nano::container_info nano::scheduler::component::container_info () const
{
	nano::container_info info;
	info.add ("hinted", hinted.container_info ());
	info.add ("manual", manual.container_info ());
	info.add ("optimistic", optimistic.container_info ());
	info.add ("priority", priority.container_info ());
	return info;
}
