#pragma once

#include <nano/lib/locks.hpp>

#include <memory>
#include <string>

namespace nano
{
class container_info_component;
class node;
}
namespace nano::scheduler
{
class hinted;
class optimistic;
class priority;

class component
{
	std::unique_ptr<nano::scheduler::hinted> hinted_impl;
	std::unique_ptr<nano::scheduler::optimistic> optimistic_impl;
	std::unique_ptr<nano::scheduler::priority> priority_impl;
	nano::mutex mutex;

public:
	explicit component (nano::node & node);
	~component ();

	// Starts all schedulers
	void start ();
	// Stops all schedulers
	void stop ();

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name);

	nano::scheduler::hinted & hinted;
	nano::scheduler::optimistic & optimistic;
	nano::scheduler::priority & priority;
};
}
