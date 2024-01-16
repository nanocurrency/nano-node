namespace nano
{
class node_flags;
}
namespace nano_daemon
{
class daemon
{
public:
	void run (std::filesystem::path const &, nano::node_flags const & flags);
};
}
