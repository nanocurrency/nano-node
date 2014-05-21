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
        dbt (mu_coin::block_id const &);
        dbt (uint16_t);
        void adopt (mu_coin::byte_write_stream &);
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
        std::unique_ptr <mu_coin::transaction_block> latest (mu_coin::address const &) override;
        std::unique_ptr <mu_coin::transaction_block> block (mu_coin::block_id const &) override;
        void insert_block (mu_coin::block_id const &, mu_coin::transaction_block const &) override;
        void insert_send (mu_coin::address const &, mu_coin::send_block const &) override;
        std::unique_ptr <mu_coin::send_block> send (mu_coin::address const &, mu_coin::block_id const &) override;
        void clear (mu_coin::address const &, mu_coin::block_id const &) override;
    private:
        void latest_sequence (mu_coin::address const &, uint16_t & sequence, bool & exists);
        Db handle;
    };
}