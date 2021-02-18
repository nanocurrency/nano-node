#include <nano/lib/errors.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/cli.hpp>
#include <nano/rpc/rpc.hpp>
#include <nano/secure/utility.hpp>
#include <nano/secure/working.hpp>

#include <boost/format.hpp>
#include <boost/make_shared.hpp>
#include <boost/program_options.hpp>

int main (int argc, char * const * argv)
{
	nano::set_umask ();
	try
	{
		boost::program_options::options_description description ("Command line options");
		description.add_options () ("help", "Print out options");
		nano::add_node_options (description);
		boost::program_options::variables_map vm;
		boost::program_options::store (boost::program_options::command_line_parser (argc, argv).options (description).allow_unregistered ().run (), vm);
		boost::program_options::notify (vm);
		int result (0);

		auto ec = nano::handle_node_options (vm);
		if (ec == nano::error_cli::unknown_command && vm.count ("help") != 0)
		{
			std::cout << description << std::endl;
		}
		return result;
	}
	catch (std::exception const & e)
	{
		std::cerr << boost::str (boost::format ("Exception while initializing %1%") % e.what ());
	}
	catch (...)
	{
		std::cerr << boost::str (boost::format ("Unknown exception while initializing"));
	}
	return 1;
}
