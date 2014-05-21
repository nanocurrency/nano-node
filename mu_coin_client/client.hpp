#pragma once

#include <mu_coin_store/db.hpp>
#include <mu_coin_wallet/wallet.hpp>
#include <mu_coin_network/network.hpp>

#include <boost/thread.hpp>

#include <QtGui>
#include <QtWidgets>

namespace mu_coin_client {
    class client
    {
    public:
        client (int, char **);
        ~client ();
        boost::asio::io_service service;
        mu_coin_store::block_store_db store;
        mu_coin::ledger ledger;
        mu_coin_wallet::wallet wallet;
        mu_coin_network::node network;
        boost::thread network_thread;
        
        QApplication application;
        QMainWindow main_window;
        QStackedWidget main_stack;
        
        QWidget settings_window;
        QVBoxLayout settings_layout;
        
        QWidget balance_main_window;
        QVBoxLayout balance_main_window_layout;
        QLabel balance_label;
        
        QWidget entry_window;
        QVBoxLayout entry_window_layout;
        QPushButton send_coins;
        QPushButton show_wallet;
        
        QWidget send_coins_window;
        QVBoxLayout send_coins_layout;
        QLabel send_address_label;
        QLineEdit send_address;
        QLabel send_count_label;
        QLineEdit send_count;
        QPushButton send_coins_send;
        QPushButton send_coins_cancel;
        
        QWidget wallet_window;
        QVBoxLayout wallet_layout;
        QStringListModel wallet_model;
        QModelIndex wallet_model_selection;
        QListView wallet_view;
        QPushButton wallet_add_account;
        QPushButton wallet_close;
        
        QMenu wallet_account_menu;
        QAction wallet_account_copy;
        QAction wallet_account_cancel;
        
        QWidget new_account_window;
        QVBoxLayout new_account_layout;
        QLabel new_account_password_label;
        QLineEdit new_account_password;
        QPushButton new_account_add_account;
        QPushButton new_account_cancel;
    private:
        QStringList keys;
    private:
        void refresh_wallet ();
    };
}