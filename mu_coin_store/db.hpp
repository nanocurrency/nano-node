#pragma once
#include <mu_coin/mu_coin.hpp>
#include <db_cxx.h>

namespace mu_coin_store {
    class dbt
    {
    public:
        dbt () = default;
        dbt (mu_coin::address const &);
        dbt (mu_coin::transaction_block const &);
        std::unique_ptr <mu_coin::transaction_block> block ();
        Dbt data;
    };
    struct block_store_db_temp_t
    {
    };
    extern block_store_db_temp_t block_store_db_temp;
    class block_store_db : public mu_coin::block_store
    {
    public:
        block_store_db (block_store_db_temp_t const &);
        std::unique_ptr <mu_coin::transaction_block> latest (mu_coin::address const &);
        void insert (mu_coin::address const &, mu_coin::transaction_block const &);
    private:
        Db handle;
    };
}