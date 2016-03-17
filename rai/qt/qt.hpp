#pragma once

#include <rai/node/node.hpp>

#include <boost/thread.hpp>

#include <set>

#include <QtGui>
#include <QtWidgets>

namespace rai_qt {
    class wallet;
	class eventloop_processor : public QObject
	{
	public:
		bool event (QEvent *) override;
	};
	class eventloop_event : public QEvent
	{
	public:
		eventloop_event (std::function <void ()> const &);
		std::function <void ()> action;
	};
    class settings
    {
    public:
        settings (rai_qt::wallet &);
        void activate ();
        void update_locked (bool, bool);
        QWidget * window;
        QVBoxLayout * layout;
        QLineEdit * password;
		QWidget * lock_window;
		QHBoxLayout * lock_layout;
        QPushButton * unlock;
        QPushButton * lock;
		QFrame * sep1;
        QLineEdit * new_password;
        QLineEdit * retype_password;
        QPushButton * change;
		QFrame * sep2;
		QLabel * representative;
		QLineEdit * new_representative;
		QPushButton * change_rep;
        QPushButton * back;
        rai_qt::wallet & wallet;
    };
    class advanced_actions
    {
    public:
		advanced_actions (rai_qt::wallet &);
		QWidget * window;
		QVBoxLayout * layout;
		QPushButton * accounts;
		QPushButton * show_ledger;
		QPushButton * show_peers;
		QPushButton * search_for_receivables;
		QPushButton * wallet_refresh;
		QPushButton * create_block;
		QPushButton * enter_block;
		QPushButton * block_viewer;
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
		QLabel * bootstrap_label;
		QLineEdit * bootstrap_line;
		QPushButton * peers_bootstrap;
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
		void refresh_balance ();
        QWidget * window;
        QVBoxLayout * layout;
        QLabel * your_account_label;
		QWidget * account_window;
		QHBoxLayout * account_layout;
        QLabel * account_text;
		QPushButton * copy_button;
        QLabel * balance_label;
        rai_qt::wallet & wallet;
    };
    class accounts
    {
    public:
        accounts (rai_qt::wallet &);
        void refresh ();
        QWidget * window;
        QVBoxLayout * layout;
        QStandardItemModel * model;
		QTableView * view;
		QPushButton * use_account;
		QPushButton * create_account;
		QPushButton * import_wallet;
		QPushButton * backup_seed;
		QFrame * separator;
        QLineEdit * account_key_line;
        QPushButton * account_key_button;
        QPushButton * back;
        rai_qt::wallet & wallet;
    };
	class import
	{
	public:
		import (rai_qt::wallet &);
		QWidget * window;
		QVBoxLayout * layout;
		QLabel * seed_label;
		QLineEdit * seed;
		QLabel * clear_label;
		QLineEdit * clear_line;
		QPushButton * import_seed;
		QFrame * separator;
		QLabel * filename_label;
		QLineEdit * filename;
		QLabel * password_label;
		QLineEdit * password;
		QPushButton * perform;
		QPushButton * back;
		rai_qt::wallet & wallet;
	};
	class history
	{
	public:
		history (rai::ledger &, rai::account const &, rai::uint128_t const &);
		void refresh ();
		QStandardItemModel * model;
		QTableView * view;
		rai::ledger & ledger;
		rai::account const & account;
		rai::uint128_t const rendering_ratio;
	};
	class block_viewer
	{
	public:
		block_viewer (rai_qt::wallet &);
		void rebroadcast_action (rai::uint256_union const &);
		QWidget * window;
		QVBoxLayout * layout;
		QLabel * hash_label;
		QLineEdit * hash;
		QLabel * block_label;
        QPlainTextEdit * block;
		QLabel * successor_label;
		QLineEdit * successor;
		QPushButton * retrieve;
		QPushButton * rebroadcast;
		QPushButton * back;
		rai_qt::wallet & wallet;
	};
	enum class status_types
	{
		not_a_status,
		disconnected,
		synchronizing,
		locked,
		vulnerable,
		active,
		nominal
	};
	class status
	{
	public:
		status (rai_qt::wallet &);
		void erase (rai_qt::status_types);
		void insert (rai_qt::status_types);
		void set_text ();
		std::string text ();
		std::string color ();
		std::set <rai_qt::status_types> active;
		rai_qt::wallet & wallet;
	};
    class wallet
    {
    public:
        wallet (QApplication &, rai::node &, std::shared_ptr <rai::wallet>, rai::account &);
        void refresh ();
		void update_connected ();
		rai::uint128_t rendering_ratio;
		rai::node & node;
		std::shared_ptr <rai::wallet> wallet_m;
		rai::account & account;
		rai_qt::eventloop_processor processor;
		rai_qt::history history;
        rai_qt::accounts accounts;
		rai_qt::self_pane self;
        rai_qt::settings settings;
        rai_qt::advanced_actions advanced;
        rai_qt::block_creation block_creation;
        rai_qt::block_entry block_entry;
		rai_qt::block_viewer block_viewer;
		rai_qt::import import;
    
        QApplication & application;
		QLabel * status;
        QStackedWidget * main_stack;
        
        QWidget * client_window;
        QVBoxLayout * client_layout;
        
        QWidget * entry_window;
        QVBoxLayout * entry_window_layout;
		QFrame * separator;
		QLabel * account_history_label;
        QPushButton * send_blocks;
		QPushButton * settings_button;
        QPushButton * show_advanced;
        
        QWidget * send_blocks_window;
        QVBoxLayout * send_blocks_layout;
        QLabel * send_account_label;
        QLineEdit * send_account;
        QLabel * send_count_label;
        QLineEdit * send_count;
        QPushButton * send_blocks_send;
        QPushButton * send_blocks_back;
		
		rai_qt::status active_status;
        void pop_main_stack ();
        void push_main_stack (QWidget *);
    };
}