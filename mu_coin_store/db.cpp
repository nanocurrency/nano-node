#include <mu_coin_store/db.hpp>
#include <boost/filesystem.hpp>

mu_coin_store::block_store_db_temp_t mu_coin_store::block_store_db_temp;

mu_coin_store::block_store_db::block_store_db (block_store_db_temp_t const &) :
handle (nullptr, 0)
{
    boost::filesystem::path temp (boost::filesystem::unique_path ());
    handle.open (nullptr, temp.native().c_str (), nullptr, DB_BTREE, DB_CREATE | DB_EXCL, 0);
}

std::unique_ptr <mu_coin::transaction_block> mu_coin_store::block_store_db::latest (mu_coin::address const & address_a)
{
    std::unique_ptr <mu_coin::transaction_block> result;
    dbt key (address_a);
    dbt value;
    int error (handle.get (nullptr, &key.data, &value.data, 0));
    if (value.data.get_size () > 0)
    {
        auto item (std::unique_ptr <mu_coin::transaction_block> (new mu_coin::transaction_block));
        mu_coin::byte_read_stream stream (reinterpret_cast <uint8_t *> (value.data.get_data ()), reinterpret_cast <uint8_t *> (value.data.get_data ()) + value.data.get_size ());
        auto error (item->deserialize (stream));
        if (!error)
        {
            result = std::move (item);
        }
    }
    return result;
}

void mu_coin_store::block_store_db::insert (mu_coin::address const & address_a, mu_coin::transaction_block const & block_a)
{
    dbt key (address_a);
    dbt data (block_a);
    int error (handle.put (nullptr, &key.data, &data.data, 0));
}

mu_coin_store::dbt::dbt (mu_coin::transaction_block const & block_a)
{
    mu_coin::byte_write_stream stream;
    block_a.serialize (stream);
    data.set_data (stream.data);
    data.set_size (stream.size);
    stream.data = nullptr;
}

mu_coin_store::dbt::dbt (mu_coin::address const & address_a)
{
    mu_coin::byte_write_stream stream;
    address_a.serialize (stream);
    data.set_data (stream.data);
    data.set_size (stream.size);
    stream.data = nullptr;
}