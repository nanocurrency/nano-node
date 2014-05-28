#include <mu_coin_qt/qt.hpp>

int main (int argc, char ** argv)
{
    boost::asio::io_service service;
    mu_coin_qt::gui gui (argc, argv, service);
    boost::thread network_thread ([&service] () {service.run ();});
    gui.main_window.show ();
    auto result (gui.application.exec ());
    network_thread.join ();
    return result;
}