#include <mu_coin_client/client.hpp>
#include <sstream>

mu_coin_client::client::client (int argc, char ** argv) :
store (mu_coin_store::block_store_db_temp),
wallet (mu_coin_wallet::wallet_temp),
application (argc, argv),
wallet_view (&main_window),
add_key ("Add Key", &main_window)
{
    connect (&add_key, &QPushButton::released, [this] () {handle_add_key ();});
}

void mu_coin_client::client::refresh_wallet ()
{
    keys = QStringList ();
    for (auto i (wallet.begin()), j (wallet.end ()); i != j; ++i)
    {
        mu_coin::EC::PublicKey key (*i);
        mu_coin::point_encoding encoding (key);
        std::stringstream stream;
        stream << std::hex;
        for (auto k (encoding.bytes.begin ()), l (encoding.bytes.end ()); k != l; ++k)
        {
            int value (*k);
            stream << value;
        }
        QString string (stream.str ().c_str ());
        keys << string;
    }
    wallet_model.setStringList (keys);
}

void mu_coin_client::client::handle_add_key ()
{
    mu_coin::keypair key;
    mu_coin::uint256_union secret;
    secret.bytes.fill (0);
    wallet.insert (key.pub, key.prv, secret);
    refresh_wallet ();
}

mu_coin_client::client::~client ()
{
}