#include <mu_coin_client/client.hpp>
#include <sstream>

mu_coin_client::client::client (int argc, char ** argv) :
store (mu_coin_store::block_store_db_temp),
ledger (store),
wallet (mu_coin_wallet::wallet_temp),
application (argc, argv),
wallet_balance_label ("Balance: 0"),
wallet_add_key ("Add Key"),
new_key_password_label ("Password:"),
new_key_add_key ("Add Key"),
new_key_cancel ("Cancel")
{
    wallet_view.setModel (&wallet_model);
    wallet_layout.addWidget (&wallet_balance_label);
    wallet_layout.addWidget (&wallet_add_key);
    wallet_layout.addWidget (&wallet_view);
    wallet_window.setLayout (&wallet_layout);
    
    new_key_layout.addWidget (&new_key_password_label);
    new_key_password.setEchoMode (QLineEdit::EchoMode::Password);
    new_key_layout.addWidget (&new_key_password);
    new_key_layout.addWidget (&new_key_add_key);
    new_key_layout.addWidget (&new_key_cancel);
    new_key_window.setLayout (&new_key_layout);
    
    main_stack.addWidget (&wallet_window);
    main_window.setCentralWidget (&main_stack);
    connect (&wallet_add_key, &QPushButton::released, [this] ()
    {
        main_stack.addWidget (&new_key_window);
        main_stack.setCurrentIndex (main_stack.count () - 1);
    });
    connect (&new_key_add_key, &QPushButton::released, [this] ()
    {
        mu_coin::keypair key;
        CryptoPP::SHA256 hash;
        QString text (new_key_password.text ());
        new_key_password.clear ();
        hash.Update (reinterpret_cast <uint8_t *> (text.data ()), text.size ());
        text.fill (0);
        mu_coin::uint256_union secret;
        hash.Final (secret.bytes.data ());
        wallet.insert (key.pub, key.prv, secret);
        refresh_wallet ();
        main_stack.removeWidget (main_stack.currentWidget ());
    });
    connect (&new_key_cancel, &QPushButton::released, [this] ()
    {
        main_stack.removeWidget (main_stack.currentWidget ());
    });
}

void mu_coin_client::client::refresh_wallet ()
{
    keys = QStringList ();
    mu_coin::uint256_t balance;
    for (auto i (wallet.begin()), j (wallet.end ()); i != j; ++i)
    {
        mu_coin::EC::PublicKey key (*i);
        balance += ledger.balance (mu_coin::address (key)).number ();
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
    wallet_balance_label.setText (QString ((std::string ("Balance: ") + balance.str ()).c_str ()));
    wallet_model.setStringList (keys);
}

mu_coin_client::client::~client ()
{
}