#pragma once

#include <celerix/node/fwd.hpp>

#include <memory>
#include <string>

namespace celerix::scheduler
{
class component final
{
public:
	component (celerix::node_config &, celerix::node &, celerix::ledger &, celerix::bucketing &, celerix::block_processor &, celerix::active_elections &, celerix::online_reps &, celerix::vote_cache &, celerix::confirming_set &, celerix::stats &, celerix::logger &);
	~component ();

	void start ();
	void stop ();

	/// Does the block exist in any of the schedulers
	bool contains (celerix::block_hash const & hash) const;

	celerix::container_info container_info () const;

private:
	std::unique_ptr<celerix::scheduler::hinted> hinted_impl;
	std::unique_ptr<celerix::scheduler::manual> manual_impl;
	std::unique_ptr<celerix::scheduler::optimistic> optimistic_impl;
	std::unique_ptr<celerix::scheduler::priority> priority_impl;

public: // Schedulers
	celerix::scheduler::hinted & hinted;
	celerix::scheduler::manual & manual;
	celerix::scheduler::optimistic & optimistic;
	celerix::scheduler::priority & priority;
};
}
