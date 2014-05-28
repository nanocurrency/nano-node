#include <mu_coin_qt/qt.hpp>

int main (int argc, char ** argv)
{
    boost::asio::io_service service;
    QApplication application (argc, argv);
    mu_coin_qt::gui gui (argc, argv, service, application);
    boost::thread network_thread ([&service] () {service.run ();});
    gui.balance_main_window.show ();
    QObject::connect (&application, &QApplication::aboutToQuit, [&gui] ()
    {
        gui.client.network.stop ();
    });
    auto result (application.exec ());
    network_thread.join ();
    return result;
}