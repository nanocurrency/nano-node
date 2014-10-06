#pragma once

#include <mu_coin/mu_coin.hpp>

#include <boost/thread.hpp>

#include <QtGui>
#include <QtWidgets>

namespace rai_qt {
    class client;
    class password_change
    {
    public:
        password_change (rai_qt::client &);
        void clear ();
        QWidget * window;
        QVBoxLayout * layout;
        QLabel * password_label;
        QLineEdit * password;
        QLabel * retype_label;
        QLineEdit * retype;
        QPushButton * change;
        QPushButton * back;
        rai_qt::client & client;
    };
    class enter_password
    {
    public:
        enter_password (rai_qt::client &);
        void activate ();
        void update_label ();
        QWidget * window;
        QVBoxLayout * layout;
        QLabel * valid;
        QLineEdit * password;
        QPushButton * unlock;
        QPushButton * lock;
        QPushButton * back;
        rai_qt::client & client;
    };
    class client
    {
    public:
        client (QApplication &, rai::client &);
        ~client ();
        rai::client & client_m;
        rai_qt::password_change password_change;
        rai_qt::enter_password enter_password;
        
        QApplication & application;
        QStackedWidget * main_stack;
        
        QWidget * settings_window;
        QVBoxLayout * settings_layout;
        QLabel * settings_port_label;
        QLabel * settings_connect_label;
        QLineEdit * settings_connect_line;
        QPushButton * settings_connect_button;
        QPushButton * settings_bootstrap_button;
        QPushButton * settings_enter_password_button;
        QPushButton * settings_change_password_button;
        QPushButton * settings_back;
        
        QWidget * client_window;
        QVBoxLayout * client_layout;
        QLabel * balance_label;
        
        QWidget * entry_window;
        QVBoxLayout * entry_window_layout;
        QPushButton * send_coins;
        QPushButton * show_wallet;
        QPushButton * settings;
        QPushButton * show_ledger;
        QPushButton * show_peers;
        QPushButton * show_log;
        
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
        QStandardItemModel * wallet_model;
        QTableView * wallet_view;
        QPushButton * wallet_refresh;
        QPushButton * wallet_add_account;
        QLineEdit * wallet_key_line;
        QPushButton * wallet_add_key_button;
        QPushButton * wallet_back;
        
        QWidget * ledger_window;
        QVBoxLayout * ledger_layout;
        QStandardItemModel * ledger_model;
        QTableView * ledger_view;
        QPushButton * ledger_refresh;
        QPushButton * ledger_back;
        
        QWidget * log_window;
        QVBoxLayout * log_layout;
        QStringListModel * log_model;
        QListView * log_view;
        QPushButton * log_refresh;
        QPushButton * log_back;
        
        QWidget * peers_window;
        QVBoxLayout * peers_layout;
        QStringListModel * peers_model;
        QListView * peers_view;
        QPushButton * peers_refresh;
        QPushButton * peers_back;
		
        void pop_main_stack ();
        void push_main_stack (QWidget *);
    private:
        void refresh_wallet ();
        void refresh_ledger ();
        void refresh_peers ();
        void refresh_log ();
    };
}