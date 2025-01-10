#pragma once

#include <memory>

namespace celerix
{
class election;
class election_insertion_result final
{
public:
	std::shared_ptr<celerix::election> election;
	bool inserted{ false };
};
}
