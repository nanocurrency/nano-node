#include <mu_coin_qt/qt.hpp>

int main (int argc, char ** argv)
{
    mu_coin_qt::gui gui (argc, argv);
    gui.main_window.show ();
    return gui.application.exec ();
}