#pragma once

#include <rai/node.hpp>

#include <boost/thread.hpp>

#include <QtGui>
#include <QtWidgets>

namespace rai_qt {
    class wallet;
    class password_change
    {
    public:
        password_change (rai_qt::wallet &);
        void clear ();
        QWidget * window;
        QVBoxLayout * layout;
        QLabel * password_label;
        QLineEdit * password;
        QLabel * retype_label;
        QLineEdit * retype;
        QPushButton * change;
        QPushButton * back;
        rai_qt::wallet & wallet;
    };
    class enter_password
    {
    public:
        enter_password (rai_qt::wallet &);
        void activate ();
        void update_label ();
        QWidget * window;
        QVBoxLayout * layout;
        QLabel * valid;
        QLineEdit * password;
        QPushButton * unlock;
        QPushButton * lock;
        QPushButton * back;
        rai_qt::wallet & wallet;
    };
    class advanced_actions
    {
    public:
        advanced_actions (rai_qt::wallet &);
        QWidget * window;
        QVBoxLayout * layout;
        QPushButton * enter_password;
        QPushButton * change_password;
        QPushButton * show_ledger;
        QPushButton * show_peers;
        QLabel * wallet_key_text;
        QLineEdit * wallet_key_line;
        QPushButton * wallet_add_key_button;
        QPushButton * search_for_receivables;
        QPushButton * wallet_refresh;
        QPushButton * create_block;
        QPushButton * enter_block;
        QPushButton * back;
        
        QWidget * ledger_window;
        QVBoxLayout * ledger_layout;
        QStandardItemModel * ledger_model;
        QTableView * ledger_view;
        QPushButton * ledger_refresh;
        QPushButton * ledger_back;
        
        QWidget * peers_window;
        QVBoxLayout * peers_layout;
        QStringListModel * peers_model;
        QListView * peers_view;
        QPushButton * peers_refresh;
        QPushButton * peers_back;
                
        rai_qt::wallet & wallet;
    private:
        void refresh_ledger ();
        void refresh_peers ();
    };
    class block_entry
    {
    public:
        block_entry (rai_qt::wallet &);
        QWidget * window;
        QVBoxLayout * layout;
        QPlainTextEdit * block;
        QLabel * status;
        QPushButton * process;
        QPushButton * back;
        rai_qt::wallet & wallet;
    };
    class block_creation
    {
    public:
        block_creation (rai_qt::wallet &);
        void deactivate_all ();
        void activate_send ();
        void activate_receive ();
        void activate_change ();
        void activate_open ();
        void create_send ();
        void create_receive ();
        void create_change ();
        void create_open ();
        QWidget * window;
        QVBoxLayout * layout;
        QButtonGroup * group;
        QHBoxLayout * button_layout;
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
        QLabel * destination_label;
        QLineEdit * destination;
        QLabel * representative_label;
        QLineEdit * representative;
        QPlainTextEdit * block;
        QLabel * status;
        QPushButton * create;
        QPushButton * back;
        rai_qt::wallet & wallet;
    };
    class self_pane
    {
    public:
        self_pane (rai_qt::wallet &, rai::account const &);
        QWidget * window;
        QVBoxLayout * layout;
        QLabel * your_account_label;
        QPushButton * account_button;
        QLabel * balance_label;
        rai_qt::wallet & wallet;
    };
    class wallet
    {
    public:
        wallet (QApplication &, rai::node &, std::shared_ptr <rai::wallet>, rai::account const &);
        ~wallet ();
        rai::node & node;
		rai_qt::self_pane self;
        rai_qt::password_change password_change;
        rai_qt::enter_password enter_password;
        rai_qt::advanced_actions advanced;
        rai_qt::block_creation block_creation;
        rai_qt::block_entry block_entry;
    
        QApplication & application;
        QStackedWidget * main_stack;
        
        QWidget * client_window;
        QVBoxLayout * client_layout;
        
        QWidget * entry_window;
        QVBoxLayout * entry_window_layout;
        QStandardItemModel * wallet_model;
        QTableView * wallet_view;
        QPushButton * send_blocks;
        QPushButton * wallet_add_account;
        QPushButton * show_advanced;
        
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
        std::shared_ptr <rai::wallet> wallet_m;
        rai::account account;
    };
}