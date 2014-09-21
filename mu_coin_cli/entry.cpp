#include <mu_coin/mu_coin.hpp>

int main ()
{
    mu_coin::system system (1, 24000, 25000, 1);
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    size_t count (10000);
    system.generate_mass_activity (count, *system.clients [0]);
}