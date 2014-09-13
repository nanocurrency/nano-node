#include <mu_coin_qt/qt.hpp>

#include <thread>

int main (int argc, char ** argv)
{
    QApplication application (argc, argv);
    auto service (boost::make_shared <boost::asio::io_service> ());
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    mu_coin::processor_service processor;
    auto client (std::make_shared <mu_coin::client> (service, pool, 24000, 25000, boost::filesystem::system_complete (argv[0]).parent_path () / "data", processor, mu_coin::genesis_address));
    std::unique_ptr <mu_coin_qt::gui> gui (new mu_coin_qt::gui (application, *client));
	gui->client_window->show ();
    std::thread network_thread ([&service] ()
    {
        try
        {
            service->run ();
        }
        catch (...)
        {
            assert (false);
        }
        std::cerr << "Network thread exited" << std::endl;
    });
    std::thread processor_thread ([&processor] ()
    {
        try
        {
            processor.run ();
        }
        catch (...)
        {
            assert (false);
        }
        std::cerr << "Processor thread exited" << std::endl;
    });
    QObject::connect (&application, &QApplication::aboutToQuit, [&] ()
    {
        client->stop ();
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