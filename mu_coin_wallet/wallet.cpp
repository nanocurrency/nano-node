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

mu_coin_wallet::key_iterator::key_iterator (Dbc * cursor_a) :
cursor (cursor_a)
{
}

mu_coin_wallet::key_iterator & mu_coin_wallet::key_iterator::operator ++ ()
{
    auto result (cursor->get (&key.data, &data.data, DB_NEXT));
    if (result == DB_NOTFOUND)
    {
        cursor->close ();
        cursor = nullptr;
    }
    return *this;
}

mu_coin::EC::PublicKey mu_coin_wallet::key_iterator::operator * ()
{
    return key.key ();
}

mu_coin::EC::PublicKey mu_coin_wallet::dbt::key ()
{
    mu_coin::point_encoding encoding;
    std::copy (reinterpret_cast <uint8_t *> (data.get_data ()), reinterpret_cast <uint8_t *> (data.get_data ()) + data.get_size (), encoding.bytes.begin ());
    return encoding.key ();
}

mu_coin_wallet::key_iterator mu_coin_wallet::wallet::begin ()
{
    Dbc * cursor;
    handle.cursor (0, &cursor, 0);
    mu_coin_wallet::key_iterator result (cursor);
    ++result;
    return result;
}

mu_coin_wallet::key_iterator mu_coin_wallet::wallet::end ()
{
    return mu_coin_wallet::key_iterator (nullptr);
}

bool mu_coin_wallet::key_iterator::operator == (mu_coin_wallet::key_iterator const & other_a) const
{
    return cursor == other_a.cursor;
}

bool mu_coin_wallet::key_iterator::operator != (mu_coin_wallet::key_iterator const & other_a) const
{
    return !(*this == other_a);
}

std::unique_ptr <mu_coin::send_block> mu_coin_wallet::wallet::send (mu_coin::ledger & ledger_a, mu_coin::address const & destination, mu_coin::uint256_t const & coins, mu_coin::uint256_union const & key)
{
    bool result (false);
    mu_coin::uint256_t amount;
    std::unique_ptr <mu_coin::send_block> send (new mu_coin::send_block);
    send->outputs.push_back (mu_coin::send_output (destination.point.key (), coins));
    for (auto i (begin ()), j (end ()); i != j && !result && amount < coins + send->fee (); ++i)
    {
        auto account (*i);
        send->inputs.push_back (mu_coin::send_input ());
        auto & input (send->inputs.back ());
        auto previous (ledger_a.previous (account));
        if (previous != nullptr)
        {
            mu_coin::uint256_t balance;
            uint16_t sequence;
            result = previous->balance (account, balance, sequence);
            if (!result)
            {
                if (amount + balance > coins + send->fee ())
                {
                    auto partial (coins + send->fee () - amount);
                    assert (partial < balance);
                    input.coins = balance - partial;
                    input.source.address = account;
                    input.source.sequence = sequence + 1;
                    amount += partial;
                }
                else
                {
                    input.coins = mu_coin::uint256_t (0);
                    input.source.address = account;
                    input.source.sequence = sequence + 1;
                    amount += balance;
                }
            }
        }
    }
    assert (amount <= coins + send->fee ());
    if (!result && amount == coins + send->fee ())
    {
        auto message (send->hash ());
        for (auto i (send->inputs.begin ()), j (send->inputs.end ()); i != j && !result; ++i)
        {
            mu_coin::EC::PrivateKey prv;
            fetch (i->source.address.point.key (), key, prv, result);
            i->sign (prv, message);
        }
    }
    else
    {
        result = true;
    }
    if (result)
    {
        send.release ();
    }
    return send;
}