#pragma once

namespace mu_coin
{
    class address;
    class block_id;
    class entry;
    class transaction_block;
}
namespace mu_coin_network
{
    class address;
    class block_id;
    class entry;
    class transaction_block;
}

void operator << (mu_coin_network::address &, mu_coin::address const &);
bool operator << (mu_coin::address &, mu_coin_network::address const &);
void operator << (mu_coin_network::block_id &, mu_coin::block_id const &);
bool operator << (mu_coin::block_id &, mu_coin_network::block_id const &);
void operator << (mu_coin_network::entry &, mu_coin::entry const &);
bool operator << (mu_coin::entry &, mu_coin_network::entry const &);
void operator << (mu_coin_network::transaction_block &, mu_coin::transaction_block const &);
bool operator << (mu_coin::transaction_block &, mu_coin_network::transaction_block const &);