#pragma once

#include <memory>

namespace nano
{
class node;
}
namespace nano::scheduler
{
class hinted;
class optimistic;
class priority;

class component
{
	std::unique_ptr<nano::scheduler::optimistic> optimistic_impl;
	std::unique_ptr<nano::scheduler::priority> priority_impl;
	std::unique_ptr<nano::scheduler::hinted> hinted_impl;

public:
	explicit component (nano::node & node);

	nano::scheduler::priority & priority;
	nano::scheduler::hinted & hinted;
	nano::scheduler::optimistic & optimistic;
};
}
