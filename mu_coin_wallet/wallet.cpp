#include <mu_coin_wallet/wallet.hpp>
#include <boost/filesystem.hpp>

mu_coin_wallet::wallet_temp_t mu_coin_wallet::wallet_temp;

mu_coin_wallet::dbt::dbt (mu_coin::EC::PublicKey const & pub)
{
    mu_coin::point_encoding encoding (pub);
    mu_coin::byte_write_stream stream;
    stream.write (encoding.bytes);
    adopt (stream);
}

mu_coin_wallet::dbt::dbt (mu_coin::EC::PrivateKey const & prv, mu_coin::uint256_union const & key)
{
    mu_coin::uint512_union encrypted (prv, key);
    mu_coin::byte_write_stream stream;
    stream.write (encrypted.bytes);
    adopt (stream);
}

void mu_coin_wallet::dbt::adopt (mu_coin::byte_write_stream & stream_a)
{
    data.set_data (stream_a.data);
    data.set_size (stream_a.size);
}

mu_coin_wallet::wallet::wallet (mu_coin_wallet::wallet_temp_t const &) :
handle (nullptr, 0)
{
    boost::filesystem::path temp (boost::filesystem::unique_path ());
    handle.open (nullptr, temp.native().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
}