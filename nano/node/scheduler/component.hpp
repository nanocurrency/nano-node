#pragma once

#include <memory>

namespace nano::scheduler
{
class buckets;
class hinted;
class node;
class optimistic;

class component
{
	std::unique_ptr<nano::scheduler::optimistic> optimistic_impl;
	std::unique_ptr<nano::scheduler::buckets> buckets_impl;
	std::unique_ptr<nano::scheduler::hinted> hinted_impl;

public:
	explicit component (nano::node & node);

	nano::scheduler::buckets & buckets;
	nano::scheduler::hinted & hinted;
	nano::scheduler::optimistic & optimistic;
};
}
