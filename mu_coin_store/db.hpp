#pragma once
#include <mu_coin/mu_coin.hpp>
#include <db_cxx.h>

namespace mu_coin_store {
    struct block_store_db_temp
    {
    };
    class block_store_db : public mu_coin::block_store
    {
    public:
        block_store_db (block_store_db_temp const &);
        std::unique_ptr <mu_coin::transaction_block> latest (mu_coin::address const &);
        void insert (mu_coin::address const &, mu_coin::transaction_block const &);
    private:
        Db handle;
    };
}