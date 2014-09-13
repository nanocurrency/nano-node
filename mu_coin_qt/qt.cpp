#include <mu_coin_qt/qt.hpp>

#include <cryptopp/sha.h>

#include <sstream>

mu_coin_qt::client::client (QApplication & application_a, mu_coin::client & client_a) :
client_m (client_a),
application (application_a),
main_stack (new QStackedWidget),
settings_window (new QWidget),
settings_layout (new QVBoxLayout),
settings_port_label (new QLabel ((std::string ("Port: ") + std::to_string (client_a.network.socket.local_endpoint ().port ())).c_str ())),
settings_connect_label (new QLabel ("Connect to IP:Port")),
settings_connect_line (new QLineEdit),
settings_connect_button (new QPushButton ("Connect")),
settings_bootstrap_button (new QPushButton ("Bootstrap")),
settings_password_label (new QLabel ("Password:")),
settings_password (new QLineEdit),
settings_back (new QPushButton ("Back")),
client_window (new QWidget),
client_layout (new QVBoxLayout),
balance_label (new QLabel),
entry_window (new QWidget),
entry_window_layout (new QVBoxLayout),
send_coins (new QPushButton ("Send")),
show_wallet (new QPushButton ("Wallet")),
settings (new QPushButton ("Settings")),
show_ledger (new QPushButton ("Ledger")),
show_peers (new QPushButton ("Peers")),
show_log (new QPushButton ("Log")),
send_coins_window (new QWidget),
send_coins_layout (new QVBoxLayout),
send_address_label (new QLabel ("Address:")),
send_address (new QLineEdit),
send_count_label (new QLabel ("Coins:")),
send_count (new QLineEdit),
send_coins_send (new QPushButton ("Send")),
send_coins_back (new QPushButton ("Back")),
wallet_window (new QWidget),
wallet_layout (new QVBoxLayout),
wallet_model (new QStringListModel),
wallet_view (new QListView),
wallet_refresh (new QPushButton ("Refresh")),
wallet_add_account (new QPushButton ("Add account")),
wallet_key_line (new QLineEdit),
wallet_add_key_button (new QPushButton ("Add key")),
wallet_back (new QPushButton ("Back")),
ledger_window (new QWidget),
ledger_layout (new QVBoxLayout),
ledger_model (new QStringListModel),
ledger_view (new QListView),
ledger_refresh (new QPushButton ("Refresh")),
ledger_back (new QPushButton ("Back")),
log_window (new QWidget),
log_layout (new QVBoxLayout),
log_model (new QStringListModel),
log_view (new QListView),
log_refresh (new QPushButton ("Refresh")),
log_back (new QPushButton ("Back")),
peers_window (new QWidget),
peers_layout (new QVBoxLayout),
peers_model (new QStringListModel),
peers_view (new QListView),
peers_refresh (new QPushButton ("Refresh")),
peers_back (new QPushButton ("Back")),
wallet_account_menu (new QMenu),
wallet_account_copy (new QAction ("Copy", wallet_account_menu)),
wallet_account_cancel (new QAction ("Cancel", wallet_account_menu))
{    
    send_coins_layout->addWidget (send_address_label);
    send_coins_layout->addWidget (send_address);
    send_coins_layout->addWidget (send_count_label);
    send_coins_layout->addWidget (send_count);
    send_coins_layout->addWidget (send_coins_send);
    send_coins_layout->addWidget (send_coins_back);
    send_coins_layout->setContentsMargins (0, 0, 0, 0);
    send_coins_window->setLayout (send_coins_layout);
    
    wallet_view->setModel (wallet_model);
    wallet_view->setContextMenuPolicy (Qt::ContextMenuPolicy::CustomContextMenu);
    wallet_layout->addWidget (wallet_view);
    wallet_layout->addWidget (wallet_refresh);
    wallet_layout->addWidget (wallet_add_account);
    wallet_layout->addWidget (wallet_key_line);
    wallet_layout->addWidget (wallet_add_key_button);
    wallet_layout->addWidget (wallet_back);
    wallet_layout->setContentsMargins (0, 0, 0, 0);
    wallet_window->setLayout (wallet_layout);
    
    wallet_account_menu->addAction (wallet_account_copy);
    wallet_account_menu->addAction (wallet_account_cancel);
    
    ledger_view->setModel (ledger_model);
    ledger_layout->addWidget (ledger_view);
    ledger_layout->addWidget (ledger_refresh);
    ledger_layout->addWidget (ledger_back);
    ledger_layout->setContentsMargins (0, 0, 0, 0);
    ledger_window->setLayout (ledger_layout);
    
    log_view->setModel (log_model);
    log_layout->addWidget (log_view);
    log_layout->addWidget (log_refresh);
    log_layout->addWidget (log_back);
    log_layout->setContentsMargins (0, 0, 0, 0);
    log_window->setLayout (log_layout);
    
    peers_view->setModel (peers_model);
    peers_layout->addWidget (peers_view);
    peers_layout->addWidget (peers_refresh);
    peers_layout->addWidget (peers_back);
    peers_layout->setContentsMargins (0, 0, 0, 0);
    peers_window->setLayout (peers_layout);
    
    entry_window_layout->addWidget (send_coins);
    entry_window_layout->addWidget (show_wallet);
    entry_window_layout->addWidget (settings);
    entry_window_layout->addWidget (show_ledger);
    entry_window_layout->addWidget (show_peers);
    entry_window_layout->addWidget (show_log);
    entry_window_layout->setContentsMargins (0, 0, 0, 0);
    entry_window->setLayout (entry_window_layout);
    
    main_stack->addWidget (entry_window);
    
    client_layout->addWidget (balance_label);
    client_layout->addWidget (main_stack);
    client_layout->setSpacing (0);
    client_window->setLayout (client_layout);
    
    settings_layout->addWidget (settings_port_label);
    settings_layout->addWidget (settings_connect_label);
    settings_layout->addWidget (settings_connect_line);
    settings_layout->addWidget (settings_connect_button);
    settings_layout->addWidget (settings_bootstrap_button);
    settings_layout->addWidget (settings_password_label);
    settings_password->setEchoMode (QLineEdit::EchoMode::Password);
    settings_layout->addWidget (settings_password);
    settings_layout->addWidget (settings_back);
    settings_window->setLayout (settings_layout);
    
    QObject::connect (log_refresh, &QPushButton::released, [this] ()
    {
        refresh_log ();
    });
    QObject::connect (log_back, &QPushButton::released, [this] ()
    {
        pop_main_stack ();
    });
    QObject::connect (show_log, &QPushButton::released, [this] ()
    {
        push_main_stack (log_window);
    });
    QObject::connect (show_peers, &QPushButton::released, [this] ()
    {
        push_main_stack (peers_window);
    });
    QObject::connect (peers_back, &QPushButton::released, [this] ()
    {
        pop_main_stack ();
    });
    QObject::connect (peers_refresh, &QPushButton::released, [this] ()
    {
        refresh_peers ();
    });
    QObject::connect (wallet_add_key_button, &QPushButton::released, [this] ()
    {
        QString key_text_wide (wallet_key_line->text ());
        std::string key_text (key_text_wide.toLocal8Bit ());
        mu_coin::private_key key;
        if (!key.decode_hex (key_text))
        {
            QPalette palette;
            palette.setColor (QPalette::Text, Qt::black);
            wallet_key_line->setPalette (palette);
            wallet_key_line->clear ();
            client_m.wallet.insert (key);
            refresh_wallet ();
        }
        else
        {
            QPalette palette;
            palette.setColor (QPalette::Text, Qt::red);
            wallet_key_line->setPalette (palette);
        }
    });
    QObject::connect (settings_bootstrap_button, &QPushButton::released, [this] ()
    {
        QString address_text_wide (settings_connect_line->text ());
        std::string address_text (address_text_wide.toLocal8Bit ());
        mu_coin::tcp_endpoint endpoint;
        if (!mu_coin::parse_tcp_endpoint (address_text, endpoint))
        {
            QPalette palette;
            palette.setColor (QPalette::Text, Qt::black);
            settings_connect_line->setPalette (palette);
            settings_bootstrap_button->setEnabled (false);
            settings_bootstrap_button->setText ("Bootstrapping...");
            client_m.processor.bootstrap (endpoint, [this] () {settings_bootstrap_button->setText ("Bootstrap"); settings_bootstrap_button->setEnabled (true);});
            settings_connect_line->clear ();
        }
        else
        {
          QPalette palette;
          palette.setColor (QPalette::Text, Qt::red);
          settings_connect_line->setPalette (palette);
        }
    });
    
    QObject::connect (settings_connect_button, &QPushButton::released, [this] ()
    {
        QString address_text_wide (settings_connect_line->text ());
        std::string address_text (address_text_wide.toLocal8Bit ());
        mu_coin::endpoint endpoint;
        if (!mu_coin::parse_endpoint (address_text, endpoint))
        {
            QPalette palette;
            palette.setColor (QPalette::Text, Qt::black);
            settings_connect_line->setPalette (palette);
            client_m.network.send_keepalive (endpoint);
            settings_connect_line->clear ();
        }
        else
        {
            QPalette palette;
            palette.setColor (QPalette::Text, Qt::red);
            settings_connect_line->setPalette (palette);
        }
    });
    QObject::connect (show_ledger, &QPushButton::released, [this] ()
    {
        push_main_stack (ledger_window);
    });
    QObject::connect (ledger_refresh, &QPushButton::released, [this] ()
    {
        refresh_ledger ();
    });
    QObject::connect (ledger_back, &QPushButton::released, [this] ()
    {
        pop_main_stack ();
    });
    QObject::connect (wallet_refresh, &QPushButton::released, [this] ()
    {
        refresh_wallet ();
    });
    QObject::connect (settings_back, &QPushButton::released, [this] ()
    {
        pop_main_stack ();
    });
    QObject::connect (settings, &QPushButton::released, [this] ()
    {
        push_main_stack (settings_window);
    });
    QObject::connect (show_wallet, &QPushButton::released, [this] ()
    {
        push_main_stack (wallet_window);
    });
    QObject::connect (wallet_back, &QPushButton::released, [this] ()
    {
        pop_main_stack ();
    });
    QObject::connect (send_coins_send, &QPushButton::released, [this] ()
    {
        QString coins_text (send_count->text ());
        std::string coins_text_narrow (coins_text.toLocal8Bit ());
        mu_coin::uint256_union coins;
        auto parse_error (coins.decode_dec (coins_text_narrow));
        if (!parse_error)
        {
            QPalette palette;
            palette.setColor (QPalette::Text, Qt::black);
            send_count->setPalette (palette);
            QString address_text (send_address->text ());
            std::string address_text_narrow (address_text.toLocal8Bit ());
            mu_coin::address address;
            parse_error = address.decode_base58check (address_text_narrow);
            if (!parse_error)
            {
                auto send_error (client_m.send (address, coins.number (), client_m.wallet.password));
                if (!send_error)
                {
                    QPalette palette;
                    palette.setColor (QPalette::Text, Qt::black);
                    send_address->setPalette (palette);
                    send_count->clear ();
                    send_address->clear ();
                    refresh_wallet ();
                }
                else
                {
                    QPalette palette;
                    palette.setColor (QPalette::Text, Qt::red);
                    send_count->setPalette (palette);
                }
            }
            else
            {
                QPalette palette;
                palette.setColor (QPalette::Text, Qt::red);
                send_address->setPalette (palette);
            }
        }
        else
        {
            QPalette palette;
            palette.setColor (QPalette::Text, Qt::red);
            send_count->setPalette (palette);
        }
    });
    QObject::connect (wallet_view, &QListView::pressed, [this] (QModelIndex const & index)
    {
        wallet_model_selection = index;
    });
    QObject::connect (wallet_account_copy, &QAction::triggered, [this] (bool)
    {
        auto & value (wallet_model->stringList ().at (wallet_model_selection.row ()));
        application.clipboard ()->setText (value);
    });
    QObject::connect (wallet_account_cancel, &QAction::triggered, [this] (bool)
    {
        wallet_account_menu->hide ();
    });
    QObject::connect (wallet_view, &QListView::customContextMenuRequested, [this] (QPoint const & pos)
    {
        wallet_account_menu->popup (wallet_view->viewport ()->mapToGlobal (pos));
    });
    QObject::connect (send_coins_back, &QPushButton::released, [this] ()
    {
        pop_main_stack ();
    });
    QObject::connect (send_coins, &QPushButton::released, [this] ()
    {
        push_main_stack (send_coins_window);
    });
    QObject::connect (settings_password, &QLineEdit::editingFinished, [this] ()
    {
/*        CryptoPP::SHA256 hash;
        QString text_w (settings_password.text ());
        std::string text (text_w.toLocal8Bit ());
        settings_password.clear ();
        hash.Update (reinterpret_cast <uint8_t const *> (text.c_str ()), text.size ());
        hash.Final (password.bytes.data ());*/
    });
    QObject::connect (wallet_add_account, &QPushButton::released, [this] ()
    {
        mu_coin::keypair key;
        client_m.wallet.insert (key.prv);
        refresh_wallet ();
    });
    refresh_wallet ();
    refresh_ledger ();
}

void mu_coin_qt::client::refresh_log ()
{
    QStringList log;
    for (auto i: client_m.log.items)
    {
        std::stringstream entry;
        entry << i.first << ' ' << i.second << std::endl;
        QString qentry (entry.str ().c_str ());
        log << qentry;
    }
    log_model->setStringList (log);
}

void mu_coin_qt::client::refresh_peers ()
{
    QStringList peers;
    for (auto i: client_m.peers.list ())
    {
        std::stringstream endpoint;
        endpoint << i.endpoint.address ().to_string ();
        endpoint << ':';
        endpoint << i.endpoint.port ();
        endpoint << ' ';
        endpoint << i.last_contact;
        endpoint << ' ';
        endpoint << i.last_attempt;
        QString qendpoint (endpoint.str().c_str ());
        peers << qendpoint;
    }
    peers_model->setStringList (peers);
}

void mu_coin_qt::client::refresh_ledger ()
{
    QStringList accounts;
    for (auto i (client_m.ledger.store.latest_begin()), j (client_m.ledger.store.latest_end ()); i != j; ++i)
    {
        std::string line;
        std::string account;
        i->first.encode_base58check (account);
        line += account;
        line += " : ";
        line += client_m.ledger.balance (i->second.hash).convert_to <std::string> ();
        line += " : ";
        std::string block_hash;
        i->second.hash.encode_hex (block_hash);
        line += block_hash;
        QString qline (line.c_str ());
        accounts << qline;
    }
    ledger_model->setStringList (accounts);
}

void mu_coin_qt::client::refresh_wallet ()
{
    QStringList keys;
    mu_coin::uint256_t balance;
    for (auto i (client_m.wallet.begin ()), j (client_m.wallet.end ()); i != j; ++i)
    {
        mu_coin::public_key key (i->first);
        auto account_balance (client_m.ledger.account_balance (key));
        balance += account_balance;
        std::string string;
        key.encode_base58check (string);
        string += " : ";
        string += account_balance.str ();
        QString qstring (string.c_str ());
        keys << qstring;
    }
    balance_label->setText (QString ((std::string ("Balance: ") + balance.str ()).c_str ()));
    wallet_model->setStringList (keys);
}

mu_coin_qt::client::~client ()
{
}

void mu_coin_qt::client::push_main_stack (QWidget * widget_a)
{
    main_stack->addWidget (widget_a);
    main_stack->setCurrentIndex (main_stack->count () - 1);
}

void mu_coin_qt::client::pop_main_stack ()
{
    main_stack->removeWidget (main_stack->currentWidget ());
}