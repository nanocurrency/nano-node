#include <rai/qt/qt.hpp>

#include <thread>

int main (int argc, char ** argv)
{
    QApplication application (argc, argv);
    static int count (16);
    rai::system system (24000, count);
    std::unique_ptr <QTabWidget> client_tabs (new QTabWidget);
    std::vector <std::unique_ptr <rai_qt::client>> guis;
    for (auto i (0); i < count; ++i)
    {
        guis.push_back (std::unique_ptr <rai_qt::client> (new rai_qt::client (application, *system.clients [i])));
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
    });
    QObject::connect (&application, &QApplication::aboutToQuit, [&] ()
    {
        for (auto & i: system.clients)
        {
            i->stop ();
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