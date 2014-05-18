#pragma once

#include <mu_coin/mu_coin.hpp>
#include <db_cxx.h>

namespace mu_coin_wallet {
    class dbt
    {
    public:
        dbt () = default;
        dbt (mu_coin::EC::PublicKey const &);
        dbt (mu_coin::EC::PrivateKey const &, mu_coin::uint256_union const &, mu_coin::uint128_union const &);
        void key (mu_coin::uint256_union const &, mu_coin::uint128_union const &, mu_coin::EC::PrivateKey &, bool &);
        mu_coin::EC::PublicKey key ();
        void adopt (mu_coin::byte_write_stream &);
        Dbt data;
    };
    struct wallet_temp_t
    {
    };
    extern wallet_temp_t wallet_temp;
    class key_iterator
    {
    public:
        key_iterator (Dbc *);
        key_iterator (mu_coin_wallet::key_iterator const &) = default;
        key_iterator & operator ++ ();
        mu_coin::EC::PublicKey operator * ();
        bool operator == (mu_coin_wallet::key_iterator const &) const;
        bool operator != (mu_coin_wallet::key_iterator const &) const;
        Dbc * cursor;
        dbt key;
        dbt data;
    };
    class wallet
    {
    public:
        wallet (wallet_temp_t const &);
        void insert (mu_coin::EC::PublicKey const &, mu_coin::EC::PrivateKey const &, mu_coin::uint256_union const &);
        void insert (mu_coin::EC::PrivateKey const &, mu_coin::uint256_union const &);
        void fetch (mu_coin::EC::PublicKey const &, mu_coin::uint256_union const &, mu_coin::EC::PrivateKey &, bool &);
        key_iterator begin ();
        key_iterator end ();
    private:
        Db handle;
    };
}