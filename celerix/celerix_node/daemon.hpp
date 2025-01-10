#include <celerix/lib/logging.hpp>

namespace celerix
{
class node_flags;

class daemon
{
	celerix::logger logger{ "daemon" };

public:
	void run (std::filesystem::path const &, celerix::node_flags const & flags);
};
}
