#include <mu_coin_store/db.hpp>

mu_coin_store::block_store_db::block_store_db (block_store_db_temp const &) :
handle (nullptr, 0)
{
}

std::unique_ptr <mu_coin::transaction_block> mu_coin_store::block_store_db::latest (mu_coin::address const &)
{
    
}

void mu_coin_store::block_store_db::insert (mu_coin::address const &, mu_coin::transaction_block const &)
{
    
}