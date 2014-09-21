#include <mu_coin/mu_coin.hpp>

int main ()
{
    /*
    mu_coin::system system (1, 24000, 25000, 1);
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    size_t count (10000);
    system.generate_mass_activity (count, *system.clients [0]);*/
    
    mu_coin::work work (32768);
    auto begin (std::chrono::high_resolution_clock::now ());
    work.perform (0, 32);
    auto end (std::chrono::high_resolution_clock::now ());
    auto us (std::chrono::duration_cast <std::chrono::microseconds> (end - begin));
    std::cout << boost::str (boost::format ("Microseconds: %1%\n") % us.count ());
}