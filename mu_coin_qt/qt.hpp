#pragma once

#include <mu_coin/mu_coin.hpp>

#include <boost/thread.hpp>

#include <QtGui>
#include <QtWidgets>

namespace mu_coin_qt {
    class gui
    {
    public:
        gui (int, char **);
        ~gui ();
        boost::asio::io_service service;
        mu_coin::client client;
        boost::thread network_thread;
        
        QApplication application;
        QMainWindow main_window;
        QStackedWidget main_stack;
        
        QWidget settings_window;
        QVBoxLayout settings_layout;
        QLabel settings_password_label;
        QLineEdit settings_password;
        QPushButton settings_close;
        
        QWidget balance_main_window;
        QVBoxLayout balance_main_window_layout;
        QLabel balance_label;
        
        QWidget entry_window;
        QVBoxLayout entry_window_layout;
        QPushButton send_coins;
        QPushButton show_wallet;
        QPushButton settings;
        
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
    private:
        void push_main_stack (QWidget *);
        void pop_main_stack ();
        void refresh_wallet ();
        mu_coin::uint256_union password;
    };
}