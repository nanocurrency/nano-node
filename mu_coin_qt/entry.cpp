#include <mu_coin_qt/qt.hpp>

#include <thread>

int main (int argc, char ** argv)
{
    QApplication application (argc, argv);
    static int count (4);
    mu_coin::system system (1, 24000, 25000, count, std::numeric_limits <mu_coin::uint256_t>::max ());
    std::unique_ptr <QTabWidget> client_tabs (new QTabWidget);
    std::vector <std::unique_ptr <mu_coin_qt::gui>> guis;
    for (auto i (0); i < count; ++i)
    {
        guis.push_back (std::unique_ptr <mu_coin_qt::gui> (new mu_coin_qt::gui {application, *system.clients [i]}));
        client_tabs->addTab (guis.back ()->client_window, boost::str (boost::format ("Client %1%") % i).c_str ());
    }
    client_tabs->show ();
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
        for (auto & i: system.clients)
        {
            i->client_m->network.stop ();
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