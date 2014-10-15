#include <rai/core/core.hpp>
#include <rai/cli/daemon.hpp>

#include <boost/program_options.hpp>

int main (int argc, char * const * argv)
{
    boost::program_options::options_description description ("Command line options");
    description.add_options ()
        ("help", "Print out options")
        ("debug_activity", "Generates fake debug activity")
        ("profile_work", "Profile the work function")
        ("generate_key", "Generates a random keypair");
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
        rai::system system (24000, 1);
        system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
        size_t count (10000);
        system.generate_mass_activity (count, *system.clients [0]);
    }
    else if (vm.count ("generate_key"))
    {
        rai::keypair pair;
        std::cout << "Private: " << pair.prv.to_string () << " Public: " << pair.pub.to_string () << std::endl;
    }
    else if (vm.count ("profile_work"))
    {
        rai::uint256_union source;
        rai::work work;
        for (auto i: work.entries)
        {
            i = 0;
        }
        auto begin1 (std::chrono::high_resolution_clock::now ());
        auto value (work.create (source));
        auto end1 (std::chrono::high_resolution_clock::now ());
        (void)work.validate (source, value);
        auto end2 (std::chrono::high_resolution_clock::now ());
        std::cerr << boost::str (boost::format ("Generation time: %1%us validation time: %2%us\n") % std::chrono::duration_cast <std::chrono::microseconds> (end1 - begin1).count () % std::chrono::duration_cast <std::chrono::microseconds> (end2 - end1).count ());
    }
    else
    {
        rai_daemon::daemon daemon;
        daemon.run ();
    }
    return result;
}