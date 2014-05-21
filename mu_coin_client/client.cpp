#include <mu_coin_client/client.hpp>

#include <sstream>

mu_coin_client::client::client (int argc, char ** argv) :
store (mu_coin_store::block_store_db_temp),
ledger (store),
wallet (mu_coin_wallet::wallet_temp),
network (service, 24000, ledger),
application (argc, argv),
send_coins ("Send"),
show_wallet ("Show wallet"),
send_address_label ("Address:"),
send_count_label ("Coins:"),
send_coins_send ("Send"),
send_coins_cancel ("Cancel"),
wallet_add_account ("Add account"),
wallet_close ("Close"),
wallet_account_copy ("Copy", &wallet_account_menu),
wallet_account_cancel ("Cancel", &wallet_account_menu),
new_account_password_label ("Password:"),
new_account_add_account ("Add account"),
new_account_cancel ("Cancel")
{
    /////////
    mu_coin::keypair genesis;
    mu_coin::uint256_union secret;
    secret.bytes.fill (0);
    wallet.insert (genesis.pub, genesis.prv, secret);
    mu_coin::transaction_block block;
    mu_coin::entry entry (genesis.pub, 1000000, 0);
    block.entries.push_back (entry);
    store.insert_block (entry.id, block);
    /////////
    
    network.receive ();
    network_thread = boost::thread ([this] () {service.run ();});
    
    send_coins_layout.addWidget (&send_address_label);
    send_coins_layout.addWidget (&send_address);
    send_coins_layout.addWidget (&send_count_label);
    send_coins_layout.addWidget (&send_count);
    send_coins_layout.addWidget (&send_coins_send);
    send_coins_layout.addWidget (&send_coins_cancel);
    send_coins_layout.setContentsMargins (0, 0, 0, 0);
    send_coins_window.setLayout (&send_coins_layout);
    
    wallet_view.setModel (&wallet_model);
    wallet_view.setContextMenuPolicy (Qt::ContextMenuPolicy::CustomContextMenu);
    wallet_layout.addWidget (&wallet_view);
    wallet_layout.addWidget (&wallet_add_account);
    wallet_layout.addWidget (&wallet_close);
    wallet_layout.setContentsMargins (0, 0, 0, 0);
    wallet_window.setLayout (&wallet_layout);
    
    wallet_account_menu.addAction (&wallet_account_copy);
    wallet_account_menu.addAction (&wallet_account_cancel);
    
    new_account_layout.addWidget (&new_account_password_label);
    new_account_password.setEchoMode (QLineEdit::EchoMode::Password);
    new_account_layout.addWidget (&new_account_password);
    new_account_layout.addWidget (&new_account_add_account);
    new_account_layout.addWidget (&new_account_cancel);
    new_account_layout.setContentsMargins (0, 0, 0, 0);
    new_account_window.setLayout (&new_account_layout);
    
    entry_window_layout.addWidget (&send_coins);
    entry_window_layout.addWidget (&show_wallet);
    entry_window_layout.addWidget (&balance_label);
    entry_window_layout.setContentsMargins (0, 0, 0, 0);
    entry_window.setLayout (&entry_window_layout);
    
    main_stack.addWidget (&entry_window);
    
    balance_main_window_layout.addWidget (&balance_label);
    balance_main_window_layout.addWidget (&main_stack);
    balance_main_window_layout.setSpacing (0);
    balance_main_window.setLayout (&balance_main_window_layout);
    
    settings_window.setLayout (&settings_layout);
    
    main_window.setCentralWidget (&balance_main_window);
    QObject::connect (&show_wallet, &QPushButton::released, [this] ()
    {
        main_stack.addWidget (&wallet_window);
        main_stack.setCurrentIndex (main_stack.count () - 1);
    });
    QObject::connect (&wallet_close, &QPushButton::released, [this] ()
    {
        main_stack.removeWidget (main_stack.currentWidget ());
    });
    QObject::connect (&send_coins_send, &QPushButton::released, [this] ()
    {
        
    });
    QObject::connect (&application, &QApplication::aboutToQuit, [this] ()
    {
        network.stop ();
        network_thread.join ();
    });
    QObject::connect (&wallet_view, &QListView::pressed, [this] (QModelIndex const & index)
    {
        wallet_model_selection = index;
    });
    QObject::connect (&wallet_account_copy, &QAction::triggered, [this] (bool)
    {
        auto & value (wallet_model.stringList ().at (wallet_model_selection.row ()));
        application.clipboard ()->setText (value);
    });
    QObject::connect (&wallet_account_cancel, &QAction::triggered, [this] (bool)
    {
        wallet_account_menu.hide ();
    });
    QObject::connect (&wallet_view, &QListView::customContextMenuRequested, [this] (QPoint const & pos)
    {
        wallet_account_menu.popup (wallet_view.viewport ()->mapToGlobal (pos));
    });
    QObject::connect (&send_coins_cancel, &QPushButton::released, [this] ()
    {
        main_stack.removeWidget (main_stack.currentWidget ());
    });
    QObject::connect (&send_coins, &QPushButton::released, [this] ()
    {
        main_stack.addWidget (&send_coins_window);
        main_stack.setCurrentIndex (main_stack.count () - 1);
    });
    QObject::connect (&wallet_add_account, &QPushButton::released, [this] ()
    {
        main_stack.addWidget (&new_account_window);
        main_stack.setCurrentIndex (main_stack.count () - 1);
    });
    QObject::connect (&new_account_add_account, &QPushButton::released, [this] ()
    {
        mu_coin::keypair key;
        CryptoPP::SHA256 hash;
        QString text (new_account_password.text ());
        new_account_password.clear ();
        hash.Update (reinterpret_cast <uint8_t *> (text.data ()), text.size ());
        text.fill (0);
        mu_coin::uint256_union secret;
        hash.Final (secret.bytes.data ());
        wallet.insert (key.pub, key.prv, secret);
        refresh_wallet ();
        main_stack.removeWidget (main_stack.currentWidget ());
    });
    QObject::connect (&new_account_cancel, &QPushButton::released, [this] ()
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
    balance_label.setText (QString ((std::string ("Balance: ") + balance.str ()).c_str ()));
    wallet_model.setStringList (keys);
}

mu_coin_client::client::~client ()
{
}