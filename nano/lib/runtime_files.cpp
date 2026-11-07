#include <nano/boost/process/pid.hpp>
#include <nano/lib/runtime_files.hpp>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
std::mutex mutex;
std::set<std::filesystem::path> registered_files;
bool atexit_registered = false;

void atexit_handler ()
{
	nano::runtime_files::cleanup ();
}
}

void nano::runtime_files::create (std::filesystem::path const & path, std::string const & contents)
{
	// Create parent directories
	std::error_code ec;
	std::filesystem::create_directories (path.parent_path (), ec);
	if (ec)
	{
		throw std::runtime_error ("Unable to create runtime file directory: " + ec.message ());
	}

	// Write file
	std::ofstream out (path, std::ios::out | std::ios::trunc);
	if (!out)
	{
		throw std::runtime_error ("Unable to open runtime file: " + path.string ());
	}
	out << contents;
	out.close ();
	if (out.fail ())
	{
		throw std::runtime_error ("Unable to write runtime file: " + path.string ());
	}

	// Register for cleanup
	std::lock_guard<std::mutex> lock (mutex);

	if (!atexit_registered)
	{
		std::atexit (atexit_handler);
		atexit_registered = true;
	}

	registered_files.insert (path);
}

void nano::runtime_files::cleanup ()
{
	std::set<std::filesystem::path> files_to_remove;
	{
		std::lock_guard<std::mutex> lock (mutex);
		files_to_remove = std::move (registered_files);
		registered_files.clear ();
	}

	for (auto const & path : files_to_remove)
	{
		std::error_code ec;
		std::filesystem::remove (path, ec);
		if (ec)
		{
			std::cerr << "WARNING: Unable to remove runtime file " << path << ": " << ec.message () << "\n";
		}
	}
}

void nano::runtime_files::create_pid_file (std::filesystem::path const & path)
{
	auto pid = boost::process::v2::current_pid ();
	nano::runtime_files::create (path, std::to_string (pid));
}

void nano::runtime_files::create_runtime_info (std::filesystem::path const & path, runtime_info const & info)
{
	boost::property_tree::ptree tree;
	tree.put ("peering_port", info.peering_port);
	if (info.rpc_port != 0)
	{
		tree.put ("rpc_port", info.rpc_port);
	}
	if (!info.node_id.empty ())
	{
		tree.put ("node_id", info.node_id);
	}

	std::ostringstream oss;
	boost::property_tree::write_json (oss, tree);
	nano::runtime_files::create (path, oss.str ());
}