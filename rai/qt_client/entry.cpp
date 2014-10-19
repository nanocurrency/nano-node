#include <rai/qt/qt.hpp>

#include <thread>

int main (int argc, char ** argv)
{
    QApplication application (argc, argv);
    auto service (boost::make_shared <boost::asio::io_service> ());
    rai::processor_service processor;
    rai::client_init init;
    auto client (std::make_shared <rai::client> (init, service, 24000, boost::filesystem::system_complete (argv[0]).parent_path () / "data", processor, rai::genesis_address));
    assert (!init.error ());
    client->start ();
    boost::system::error_code ec;
    std::vector <std::pair <std::string, std::string>> well_known;
    well_known.push_back (std::make_pair ("raiblocks.net", "24000"));
    client->processor.find_network (well_known);
    assert (!ec);
    std::unique_ptr <rai_qt::client> gui (new rai_qt::client (application, *client));
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
    });
    QObject::connect (&application, &QApplication::aboutToQuit, [&] ()
    {
        client->stop ();
        processor.stop ();
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