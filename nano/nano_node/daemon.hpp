#include <nano/lib/logging.hpp>

namespace nano
{
class node_flags;

class daemon
{
	nano::logger logger{ "daemon" };

public:
	void run (std::filesystem::path const &, nano::node_flags const & flags);
};
}
