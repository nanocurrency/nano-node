#include <mu_coin_qt/qt.hpp>

#include <cryptopp/sha.h>

#include <sstream>

mu_coin_qt::gui::gui (int argc, char ** argv, boost::asio::io_service & service_a, QApplication & application_a, mu_coin::processor_service & processor_a) :
client (service_a, 24000, boost::filesystem::unique_path (), boost::filesystem::unique_path (), processor_a),
application (application_a),
settings_password_label ("Password:"),
settings_close ("Close"),
send_coins ("Send"),
show_wallet ("Show wallet"),
settings ("Settings"),
send_address_label ("Address:"),
send_count_label ("Coins:"),
send_coins_send ("Send"),
send_coins_cancel ("Cancel"),
wallet_add_account ("Add account"),
wallet_close ("Close"),
wallet_account_copy ("Copy", &wallet_account_menu),
wallet_account_cancel ("Cancel", &wallet_account_menu)
{
    /////////
    mu_coin::keypair genesis;
    client.wallet.insert (genesis.pub, genesis.prv, password);
    client.store.genesis_put (genesis.pub, 1000000);
    /////////
    
    client.network.receive ();
    
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
    
    entry_window_layout.addWidget (&send_coins);
    entry_window_layout.addWidget (&show_wallet);
    entry_window_layout.addWidget (&settings);
    entry_window_layout.setContentsMargins (0, 0, 0, 0);
    entry_window.setLayout (&entry_window_layout);
    
    main_stack.addWidget (&entry_window);
    
    balance_main_window_layout.addWidget (&balance_label);
    balance_main_window_layout.addWidget (&main_stack);
    balance_main_window_layout.setSpacing (0);
    balance_main_window.setLayout (&balance_main_window_layout);
    
    settings_layout.addWidget (&settings_password_label);
    settings_password.setEchoMode (QLineEdit::EchoMode::Password);
    settings_layout.addWidget (&settings_password);
    settings_layout.addWidget (&settings_close);
    settings_window.setLayout (&settings_layout);
    
    QObject::connect (&settings_close, &QPushButton::released, [this] ()
    {
        pop_main_stack ();
    });
    QObject::connect (&settings, &QPushButton::released, [this] ()
    {
        push_main_stack (&settings_window);
    });
    QObject::connect (&show_wallet, &QPushButton::released, [this] ()
    {
        push_main_stack (&wallet_window);
    });
    QObject::connect (&wallet_close, &QPushButton::released, [this] ()
    {
        pop_main_stack ();
    });
    QObject::connect (&send_coins_send, &QPushButton::released, [this] ()
    {
        QString coins_text (send_count.text ());
        std::string coins_text_narrow (coins_text.toLocal8Bit ());
        mu_coin::uint256_union coins;
        auto parse_error (coins.decode_dec (coins_text_narrow));
        if (!parse_error)
        {
            QPalette palette;
            palette.setColor (QPalette::Text, Qt::black);
            send_count.setPalette (palette);
            QString address_text (send_address.text ());
            std::string address_text_narrow (address_text.toLocal8Bit ());
            mu_coin::address address;
            parse_error = address.decode_hex (address_text_narrow);
            if (!parse_error)
            {
                QPalette palette;
                palette.setColor (QPalette::Text, Qt::black);
                send_address.setPalette (palette);
                auto send_error (client.send (address, coins.number (), password));
                if (!send_error)
                {
                    send_count.clear ();
                    send_address.clear ();
                    refresh_wallet ();
                }
                else
                {
                    QPalette palette;
                    palette.setColor (QPalette::Text, Qt::red);
                    send_count.setPalette (palette);
                }
            }
            else
            {
                QPalette palette;
                palette.setColor (QPalette::Text, Qt::red);
                send_address.setPalette (palette);
            }
        }
        else
        {
            QPalette palette;
            palette.setColor (QPalette::Text, Qt::red);
            send_count.setPalette (palette);
        }
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
        pop_main_stack ();
    });
    QObject::connect (&send_coins, &QPushButton::released, [this] ()
    {
        push_main_stack (&send_coins_window);
    });
    QObject::connect (&settings_password, &QLineEdit::editingFinished, [this] ()
    {
        CryptoPP::SHA256 hash;
        QString text_w (settings_password.text ());
        std::string text (text_w.toLocal8Bit ());
        settings_password.clear ();
        hash.Update (reinterpret_cast <uint8_t const *> (text.c_str ()), text.size ());
        hash.Final (password.bytes.data ());
    });
    QObject::connect (&wallet_add_account, &QPushButton::released, [this] ()
    {
        mu_coin::keypair key;
        client.wallet.insert (key.pub, key.prv, password);
        refresh_wallet ();
    });
    refresh_wallet ();
}

void mu_coin_qt::gui::refresh_wallet ()
{
    QStringList keys;
    mu_coin::uint256_t balance;
    for (auto i (client.wallet.begin ()), j (client.wallet.end ()); i != j; ++i)
    {
        mu_coin::public_key key (i->first);
        auto account_balance (client.ledger.balance (key));
        balance += account_balance;
        std::string string;
        key.encode_hex (string);
        string += ":";
        string += account_balance.str ();
        QString qstring (string.c_str ());
        keys << qstring;
    }
    balance_label.setText (QString ((std::string ("Balance: ") + balance.str ()).c_str ()));
    wallet_model.setStringList (keys);
}

mu_coin_qt::gui::~gui ()
{
}

void mu_coin_qt::gui::push_main_stack (QWidget * widget_a)
{
    main_stack.addWidget (widget_a);
    main_stack.setCurrentIndex (main_stack.count () - 1);
}

void mu_coin_qt::gui::pop_main_stack ()
{
    main_stack.removeWidget (main_stack.currentWidget ());
}