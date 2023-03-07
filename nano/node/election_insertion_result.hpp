#pragma once

#include <memory>

namespace nano
{
class election;
class election_insertion_result final
{
public:
	std::shared_ptr<nano::election> election;
	bool inserted{ false };
};
}
