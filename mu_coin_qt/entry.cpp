#include <mu_coin_qt/qt.hpp>

#include <thread>

int main (int argc, char ** argv)
{
    QApplication application (argc, argv);
    static int count (2);
    mu_coin::system system (1, 24000, 25000, count, std::numeric_limits <mu_coin::uint256_t>::max ());
    std::vector <std::unique_ptr <mu_coin_qt::gui>> guis;
    guis.reserve (count);
    for (auto i (0); i < count; ++i)
    {
        guis.push_back (std::unique_ptr <mu_coin_qt::gui> {new mu_coin_qt::gui {application, *system.clients [i]}});
        guis.back ()->balance_main_window->show ();
    }
    std::thread network_thread ([&system] ()
    {
        try
        {
            system.service->run ();
        }
        catch (...)
        {
            assert (false);
        }
        std::cerr << "Network thread exited" << std::endl;
    });
    std::thread processor_thread ([&system] ()
    {
        try
        {
            system.processor.run ();
        }
        catch (...)
        {
            assert (false);
        }
        std::cerr << "Processor thread exited" << std::endl;
    });
    QObject::connect (&application, &QApplication::aboutToQuit, [&] ()
    {
        for (auto & i: guis)
        {
            i->client.network.stop ();
        }
        system.processor.stop ();
    });
    int result;
    try
    {
        result = application.exec ();
    }
    catch (...)
    {
        result = -1;
        assert (false);
    }
    network_thread.join ();
    processor_thread.join ();
    return result;
}