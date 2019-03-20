#include <boost/filesystem.hpp>

namespace nano
{
class node;
class rpc;
class node_flags;
namespace ipc
{
	class ipc_server;
}
}
namespace nano_daemon
{
class daemon
{
public:
	daemon (boost::filesystem::path const & data_path, nano::node_flags const & flags);
	~daemon ();
	void run ();
	void stop ();
	boost::filesystem::path const & data_path;
	nano::node_flags const & flags;
	std::shared_ptr<nano::node> node;
	std::shared_ptr<nano::rpc> rpc;
	std::shared_ptr<nano::ipc::ipc_server> ipc;
};
}
