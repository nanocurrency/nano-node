#pragma once

#include <mu_coin/mu_coin.hpp>

#include <boost/thread.hpp>

#include <QtGui>
#include <QtWidgets>

namespace mu_coin_qt {
    class gui
    {
    public:
        gui (QApplication &, mu_coin::client &);
        ~gui ();
        mu_coin::client & client;
        
        QApplication & application;
        QStackedWidget * main_stack;
        
        QWidget * settings_window;
        QVBoxLayout * settings_layout;
        QLabel * settings_port_label;
        QLabel * settings_password_label;
        QLineEdit * settings_password;
        QPushButton * settings_back;
        
        QWidget * balance_main_window;
        QVBoxLayout * balance_main_window_layout;
        QLabel * balance_label;
        
        QWidget * entry_window;
        QVBoxLayout * entry_window_layout;
        QPushButton * send_coins;
        QPushButton * show_wallet;
        QPushButton * settings;
        QPushButton * show_ledger;
        
        QWidget * send_coins_window;
        QVBoxLayout * send_coins_layout;
        QLabel * send_address_label;
        QLineEdit * send_address;
        QLabel * send_count_label;
        QLineEdit * send_count;
        QPushButton * send_coins_send;
        QPushButton * send_coins_back;
        
        QWidget * wallet_window;
        QVBoxLayout * wallet_layout;
        QStringListModel * wallet_model;
        QModelIndex wallet_model_selection;
        QListView * wallet_view;
        QPushButton * wallet_refresh;
        QPushButton * wallet_add_account;
        QPushButton * wallet_back;
        
        QWidget * ledger_window;
        QVBoxLayout * ledger_layout;
        QStringListModel * ledger_model;
        QListView * ledger_view;
        QPushButton * ledger_refresh;
        QPushButton * ledger_back;
        
        QMenu * wallet_account_menu;
        QAction * wallet_account_copy;
        QAction * wallet_account_cancel;
    private:
        void push_main_stack (QWidget *);
        void pop_main_stack ();
        void refresh_wallet ();
        void refresh_ledger ();
    };
}