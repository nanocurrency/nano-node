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

mu_coin_wallet::dbt::dbt (mu_coin::EC::PrivateKey const & prv, mu_coin::uint256_union const & key, mu_coin::uint128_union const & iv)
{
    mu_coin::uint256_union encrypted (prv, key, iv);
    mu_coin::byte_write_stream stream;
    stream.write (encrypted.bytes);
    adopt (stream);
}

void mu_coin_wallet::dbt::adopt (mu_coin::byte_write_stream & stream_a)
{
    data.set_data (stream_a.data);
    data.set_size (stream_a.size);
    stream_a.data = nullptr;
}

mu_coin_wallet::wallet::wallet (mu_coin_wallet::wallet_temp_t const &) :
handle (nullptr, 0)
{
    boost::filesystem::path temp (boost::filesystem::unique_path ());
    handle.open (nullptr, temp.native().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
}

void mu_coin_wallet::wallet::insert (mu_coin::EC::PublicKey const & pub, mu_coin::EC::PrivateKey const & prv, mu_coin::uint256_union const & key_a)
{
    mu_coin::point_encoding encoding (pub);
    dbt key (pub);
    dbt value (prv, key_a, encoding.iv ());
    auto error (handle.put (0, &key.data, &value.data, 0));
    assert (error == 0);
}

void mu_coin_wallet::wallet::insert (mu_coin::EC::PrivateKey const & prv, mu_coin::uint256_union const & key)
{
    mu_coin::EC::PublicKey pub;
    prv.MakePublicKey (pub);
    insert (pub, prv, key);
}

void mu_coin_wallet::wallet::fetch (mu_coin::EC::PublicKey const & pub, mu_coin::uint256_union const & key_a, mu_coin::EC::PrivateKey & prv, bool & failure)
{
    dbt key (pub);
    dbt value;
    failure = false;
    auto error (handle.get (0, &key.data, &value.data, 0));
    if (error == 0)
    {
        mu_coin::point_encoding encoding (pub);
        value.key (key_a, encoding.iv (), prv, failure);
        if (!failure)
        {
            mu_coin::EC::PublicKey compare;
            prv.MakePublicKey (compare);
            if (!(pub == compare))
            {
                failure = true;
            }
        }
    }
    else
    {
        failure = true;
    }
}

void mu_coin_wallet::dbt::key (mu_coin::uint256_union const & key_a, mu_coin::uint128_union const & iv, mu_coin::EC::PrivateKey & prv, bool & failure)
{
    mu_coin::uint256_union encrypted;
    mu_coin::byte_read_stream stream (reinterpret_cast <uint8_t *> (data.get_data ()), data.get_size ());
    failure = stream.read (encrypted.bytes);
    if (!failure)
    {
        prv = encrypted.key (key_a, iv);
    }
}