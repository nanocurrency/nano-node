#include <nano/lib/config.hpp>

#include <boost/filesystem/path.hpp>

#include <valgrind/valgrind.h>

namespace nano
{
const char * network_constants::active_network_err_msg = "Invalid network. Valid values are live, beta and test.";

void force_nano_test_network ()
{
	nano::network_constants::set_active_network (nano::nano_networks::nano_test_network);
}

bool running_within_valgrind ()
{
	return (RUNNING_ON_VALGRIND > 0);
}

std::string get_config_path (boost::filesystem::path const & data_path)
{
	return (data_path / "config.json").string ();
}

std::string get_rpc_config_path (boost::filesystem::path const & data_path)
{
	return (data_path / "rpc_config.json").string ();
}

std::string get_node_toml_config_path (boost::filesystem::path const & data_path)
{
	return (data_path / "config-node.toml").string ();
}

std::string get_rpc_toml_config_path (boost::filesystem::path const & data_path)
{
	return (data_path / "config-rpc.toml").string ();
}

std::string get_qtwallet_toml_config_path (boost::filesystem::path const & data_path)
{
	return (data_path / "config-qtwallet.toml").string ();
}
}
