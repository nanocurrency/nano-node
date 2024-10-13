#pragma once

#include <nano/node/fwd.hpp>

#include <memory>
#include <string>

namespace nano::scheduler
{
class component final
{
public:
	explicit component (nano::node & node);
	~component ();

	// Starts all schedulers
	void start ();
	// Stops all schedulers
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
