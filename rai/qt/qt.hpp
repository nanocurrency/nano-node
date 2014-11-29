#pragma once

#include <rai/core/core.hpp>

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
    class advanced_actions
    {
    public:
        advanced_actions (rai_qt::client &);
        QWidget * window;
        QVBoxLayout * layout;
        QPushButton * show_ledger;
        QPushButton * show_peers;
        QPushButton * show_log;
        QLabel * wallet_key_text;
        QLineEdit * wallet_key_line;
        QPushButton * wallet_add_key_button;
        QPushButton * enter_block;
        QPushButton * back;
        
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
                
        rai_qt::client & client;
    private:
        void refresh_ledger ();
        void refresh_peers ();
        void refresh_log ();
    };
    class block_entry
    {
    public:
        block_entry (rai_qt::client &);
        QWidget * window;
        QVBoxLayout * layout;
        QPlainTextEdit * block;
        QLabel * status;
        QPushButton * process;
        QPushButton * back;
        rai_qt::client & client;
    };
    class block_creation
    {
    public:
        block_creation (rai_qt::client &);
        QWidget * window;
        QVBoxLayout * layout;
        QButtonGroup * type_group;
        QRadioButton * send;
        QRadioButton * receive;
        QRadioButton * change;
        QRadioButton * open;
        QLabel * account_label;
        QLineEdit * account;
        QLabel * source_label;
        QLineEdit * source;
        QLabel * amount_label;
        QLineEdit * amount;
        QLabel * representative_label;
        QLineEdit * representative;
        QPushButton * back;
    };
    class client
    {
    public:
        client (QApplication &, rai::client &);
        ~client ();
        rai::client & client_m;
        rai_qt::password_change password_change;
        rai_qt::enter_password enter_password;
        rai_qt::advanced_actions advanced;
        rai_qt::block_entry block_entry;
        
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
        QStandardItemModel * wallet_model;
        QTableView * wallet_view;
        QPushButton * send_blocks;
        QPushButton * wallet_add_account;
        QPushButton * settings;
        QPushButton * show_advanced;
        QPushButton * wallet_refresh;
        
        QWidget * send_blocks_window;
        QVBoxLayout * send_blocks_layout;
        QLabel * send_account_label;
        QLineEdit * send_account;
        QLabel * send_count_label;
        QLineEdit * send_count;
        QPushButton * send_blocks_send;
        QPushButton * send_blocks_back;
		
        void pop_main_stack ();
        void push_main_stack (QWidget *);
        void refresh_wallet ();
    };
}