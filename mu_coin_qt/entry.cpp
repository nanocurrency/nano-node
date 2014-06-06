#include <mu_coin_qt/qt.hpp>

#include <thread>

int main (int argc, char ** argv)
{
    boost::asio::io_service service;
    mu_coin::processor_service processor;
    QApplication application (argc, argv);
    mu_coin_qt::gui gui (argc, argv, service, application, processor);
    std::thread network_thread ([&service] () {service.run ();});
    std::thread processor_thread ([&processor] () {processor.run ();});
    gui.balance_main_window.show ();
    QObject::connect (&application, &QApplication::aboutToQuit, [&] ()
    {
        gui.client.network.stop ();
        processor.stop ();
    });
    auto result (application.exec ());
    network_thread.join ();
    processor_thread.join ();
    return result;
}