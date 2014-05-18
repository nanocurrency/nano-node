#include <mu_coin_client/client.hpp>

int main (int argc, char ** argv)
{
    mu_coin_client::client client (argc, argv);
    client.main_window.show ();
    return client.application.exec ();
}