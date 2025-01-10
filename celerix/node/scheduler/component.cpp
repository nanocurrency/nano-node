#include <celerix/node/node.hpp>
#include <celerix/node/scheduler/component.hpp>
#include <celerix/node/scheduler/hinted.hpp>
#include <celerix/node/scheduler/manual.hpp>
#include <celerix/node/scheduler/optimistic.hpp>
#include <celerix/node/scheduler/priority.hpp>

celerix::scheduler::component::component (celerix::node_config & node_config, celerix::node & node, celerix::ledger & ledger, celerix::bucketing & bucketing, celerix::block_processor & block_processor, celerix::active_elections & active, celerix::online_reps & online_reps, celerix::vote_cache & vote_cache, celerix::confirming_set & confirming_set, celerix::stats & stats, celerix::logger & logger) :
	hinted_impl{ std::make_unique<celerix::scheduler::hinted> (node_config.hinted_scheduler, node, vote_cache, active, online_reps, stats) },
	manual_impl{ std::make_unique<celerix::scheduler::manual> (node) },
	optimistic_impl{ std::make_unique<celerix::scheduler::optimistic> (node_config.optimistic_scheduler, node, ledger, active, node_config.network_params.network, stats) },
	priority_impl{ std::make_unique<celerix::scheduler::priority> (node_config, node, ledger, bucketing, block_processor, active, confirming_set, stats, logger) },
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

celerix::scheduler::component::~component ()
{
}

void celerix::scheduler::component::start ()
{
	hinted.start ();
	manual.start ();
	optimistic.start ();
	priority.start ();
}

void celerix::scheduler::component::stop ()
{
	hinted.stop ();
	manual.stop ();
	optimistic.stop ();
	priority.stop ();
}

bool celerix::scheduler::component::contains (celerix::block_hash const & hash) const
{
	return manual.contains (hash) || priority.contains (hash);
}

celerix::container_info celerix::scheduler::component::container_info () const
{
	celerix::container_info info;
	info.add ("hinted", hinted.container_info ());
	info.add ("manual", manual.container_info ());
	info.add ("optimistic", optimistic.container_info ());
	info.add ("priority", priority.container_info ());
	return info;
}
