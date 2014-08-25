#include <mu_coin/mu_coin.hpp>

int main ()
{
    mu_coin::system system (1, 24000, 25000, 1, std::numeric_limits <mu_coin::uint256_t>::max ());
    system.clients [0]->client_m->wallet.insert (system.test_genesis_address.prv, system.clients [0]->client_m->wallet.password);
    size_t count (1000);
    system.generate_mass_activity (count, *system.clients [0]->client_m);
}