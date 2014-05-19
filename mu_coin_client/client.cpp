#include <mu_coin_client/client.hpp>
#include <sstream>

mu_coin_client::client::client (int argc, char ** argv) :
store (mu_coin_store::block_store_db_temp),
ledger (store),
wallet (mu_coin_wallet::wallet_temp),
network (service, 24000, ledger),
application (argc, argv),
send_address_label ("Address:"),
send_count_label ("Coins:"),
send_coins_send ("Send"),
send_coins_cancel ("Cancel"),
send_coins ("Send"),
wallet_add_key ("Add Key"),
wallet_key_copy ("Copy", &wallet_key_menu),
wallet_key_cancel ("Cancel", &wallet_key_menu),
new_key_password_label ("Password:"),
new_key_add_key ("Add Key"),
new_key_cancel ("Cancel")
{
    /////////
    mu_coin::keypair genesis;
    mu_coin::uint256_union secret;
    secret.bytes.fill (0);
    wallet.insert (genesis.pub, genesis.prv, secret);
    mu_coin::transaction_block block;
    mu_coin::entry entry (genesis.pub, 1000000, 0);
    block.entries.push_back (entry);
    store.insert (entry.id, block);
    /////////
    
    network.receive ();
    network_thread = boost::thread ([this] () {service.run ();});
    
    send_coins_layout.addWidget (&send_address_label);
    send_coins_layout.addWidget (&send_address);
    send_coins_layout.addWidget (&send_count_label);
    send_coins_layout.addWidget (&send_count);
    send_coins_layout.addWidget (&send_coins_send);
    send_coins_layout.addWidget (&send_coins_cancel);
    send_coins_window.setLayout (&send_coins_layout);
    
    wallet_view.setModel (&wallet_model);
    wallet_view.setContextMenuPolicy (Qt::ContextMenuPolicy::CustomContextMenu);
    wallet_layout.addWidget (&wallet_balance_label);
    wallet_layout.addWidget (&wallet_add_key);
    wallet_layout.addWidget (&send_coins);
    wallet_layout.addWidget (&wallet_view);
    wallet_window.setLayout (&wallet_layout);
    
    wallet_key_menu.addAction (&wallet_key_copy);
    wallet_key_menu.addAction (&wallet_key_cancel);
    
    new_key_layout.addWidget (&new_key_password_label);
    new_key_password.setEchoMode (QLineEdit::EchoMode::Password);
    new_key_layout.addWidget (&new_key_password);
    new_key_layout.addWidget (&new_key_add_key);
    new_key_layout.addWidget (&new_key_cancel);
    new_key_window.setLayout (&new_key_layout);
    
    main_stack.addWidget (&wallet_window);
    main_window.setCentralWidget (&main_stack);
    connect (&send_coins_send, &QPushButton::released, [this] ()
    {
        
    });
    connect (&application, &QApplication::aboutToQuit, [this] ()
    {
        network.stop ();
        network_thread.join ();
    });
    connect (&wallet_view, &QListView::pressed, [this] (QModelIndex const & index)
    {
        wallet_model_selection = index;
    });
    connect (&wallet_key_copy, &QAction::triggered, [this] (bool)
    {
        auto & value (wallet_model.stringList ().at (wallet_model_selection.row ()));
        application.clipboard ()->setText (value);
    });
    connect (&wallet_key_cancel, &QAction::triggered, [this] (bool)
    {
        wallet_key_menu.hide ();
    });
    connect (&wallet_view, &QListView::customContextMenuRequested, [this] (QPoint const & pos)
    {
        wallet_key_menu.popup (wallet_view.viewport ()->mapToGlobal (pos));
    });
    connect (&send_coins_cancel, &QPushButton::released, [this] ()
    {
        main_stack.removeWidget (main_stack.currentWidget ());
    });
    connect (&send_coins, &QPushButton::released, [this] ()
    {
        main_stack.addWidget (&send_coins_window);
        main_stack.setCurrentIndex (main_stack.count () - 1);
    });
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
    refresh_wallet ();
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