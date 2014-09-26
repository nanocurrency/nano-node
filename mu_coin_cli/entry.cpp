#include <mu_coin/mu_coin.hpp>
#include <mu_coin_cli/daemon.hpp>

#include <boost/program_options.hpp>

int main (int argc, char * const * argv)
{
    boost::program_options::options_description description ("Command line options");
    description.add_options ()
        ("help", "Print out options")
        ("debug_activity", "Generates fake debug activity");
    boost::program_options::variables_map vm;
    boost::program_options::store (boost::program_options::parse_command_line(argc, argv, description), vm);
    boost::program_options::notify (vm);
    int result (0);
    if (vm.count ("help"))
    {
        std::cout << description << std::endl;
        result = -1;
    }
    else if (vm.count ("debug_activity"))
    {
        mu_coin::system system (1, 24000, 25000, 1);
        system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
        size_t count (10000);
        system.generate_mass_activity (count, *system.clients [0]);
    }
    else
    {
        mu_coin_daemon::daemon daemon;
        daemon.run ();
    }
    return result;
}