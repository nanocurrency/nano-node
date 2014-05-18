#include <mu_coin_wallet/wallet.hpp>

mu_coin_wallet::wallet_temp_t wallet_temp;

mu_coin_wallet::dbt::dbt (mu_coin::EC::PublicKey const & pub)
{
    mu_coin::point_encoding encoding (pub);
    mu_coin::byte_write_stream stream;
    stream.write (encoding.bytes);
    adopt (stream);
}

mu_coin_wallet::dbt::dbt (mu_coin::uint512_union const & prv)
{
    
}

void mu_coin_wallet::dbt::adopt (mu_coin::byte_write_stream & stream_a)
{
    data.set_data (stream_a.data);
    data.set_size (stream_a.size);
}