#pragma once

#include <mu_coin/mu_coin.hpp>
#include <db_cxx.h>

namespace mu_coin_wallet {
    class dbt
    {
    public:
        dbt (mu_coin::EC::PublicKey const &);
        dbt (mu_coin::uint512_union const & prv);
        void adopt (mu_coin::byte_write_stream &);
        Dbt data;
    };
    struct wallet_temp_t
    {
    };
    extern wallet_temp_t wallet_temp;
    class wallet
    {
    public:
        wallet (wallet_temp_t const &);
    private:
        Db handle;
    };
}