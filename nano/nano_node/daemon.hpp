#include <nano/lib/logging.hpp>

namespace nano
{
class node_flags;

class daemon
{
	nano::nlogger nlogger{ "daemon" };

public:
	void run (std::filesystem::path const &, nano::node_flags const & flags);
};
}
