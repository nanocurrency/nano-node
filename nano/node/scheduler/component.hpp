#pragma once

#include <nano/node/fwd.hpp>

#include <memory>
#include <string>

namespace nano::scheduler
{
class component final
{
public:
	component (nano::node_config &, nano::node &, nano::ledger &, nano::block_processor &, nano::active_elections &, nano::online_reps &, nano::vote_cache &, nano::stats &, nano::logger &);
	~component ();

	void start ();
	void stop ();

	nano::container_info container_info () const;

private:
	std::unique_ptr<nano::scheduler::hinted> hinted_impl;
	std::unique_ptr<nano::scheduler::manual> manual_impl;
	std::unique_ptr<nano::scheduler::optimistic> optimistic_impl;
	std::unique_ptr<nano::scheduler::priority> priority_impl;

public: // Schedulers
	nano::scheduler::hinted & hinted;
	nano::scheduler::manual & manual;
	nano::scheduler::optimistic & optimistic;
	nano::scheduler::priority & priority;
};
}
