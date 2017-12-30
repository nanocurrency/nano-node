#pragma once

#include <rai/node/node.hpp>

#include <boost/thread.hpp>

#include <set>

#include <QObject>

#include <QtGui>
#include <QtWidgets>

class SendResult : public QObject
{
	Q_OBJECT
public:
	SendResult (QObject * parent = nullptr);
	enum class Type
	{
		WalletIsLocked,
		NotEnoughBalance,
		BadDestinationAccount,
		AmountTooBig,
		BadAmountNumber,
		BlockSendFailed,
		Success
	};
	Q_ENUMS (Type)
};
Q_DECLARE_METATYPE (SendResult::Type)

namespace rai_qt
{
class wallet;
class eventloop_processor : public QObject
{
public:
	bool event (QEvent *) override;
};
class eventloop_event : public QEvent
{
public:
	eventloop_event (std::function<void()> const &);
	std::function<void()> action;
};
class settings
{
public:
	settings (rai_qt::wallet &);
	void refresh_representative ();
	void activate ();
	void update_locked (bool, bool);
	QWidget * window;
	QVBoxLayout * layout;
	QLineEdit * password;
	QPushButton * lock_toggle;
	QFrame * sep1;
	QLineEdit * new_password;
	QLineEdit * retype_password;
	QPushButton * change;
	QFrame * sep2;
	QLabel * representative;
	QLabel * current_representative;
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
	QPushButton * show_ledger;
	QPushButton * show_peers;
	QPushButton * search_for_receivables;
	QPushButton * bootstrap;
	QPushButton * wallet_refresh;
	QPushButton * create_block;
	QPushButton * enter_block;
	QPushButton * block_viewer;
	QPushButton * account_viewer;
	QWidget * scale_window;
	QHBoxLayout * scale_layout;
	QLabel * scale_label;
	QButtonGroup * ratio_group;
	QRadioButton * mrai;
	QRadioButton * krai;
	QRadioButton * rai;
	QPushButton * back;

	QWidget * ledger_window;
	QVBoxLayout * ledger_layout;
	QStandardItemModel * ledger_model;
	QTableView * ledger_view;
	QPushButton * ledger_refresh;
	QPushButton * ledger_back;

	QWidget * peers_window;
	QVBoxLayout * peers_layout;
	QStandardItemModel * peers_model;
	QTableView * peers_view;
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
class self_pane : public QObject
{
	Q_OBJECT
	Q_PROPERTY (QString account READ getAccount NOTIFY accountChanged)
	Q_PROPERTY (QString balance READ getBalance NOTIFY balanceChanged)
	Q_PROPERTY (QString pending READ getPending NOTIFY pendingChanged)
public:
	self_pane (rai_qt::wallet &, rai::account const &);
	void refresh_balance ();
	rai_qt::wallet & wallet;

	QString getAccount ();
	void setAccount (QString account);

	QString getBalance ();
	QString getPending ();

	Q_SIGNAL void accountChanged (QString account);
	Q_SIGNAL void balanceChanged (QString balance);
	Q_SIGNAL void pendingChanged (QString pending);

private:
	QString m_account;
	QString m_balance;
	QString m_pending;

	void setBalance (QString balance);
	void setPending (QString pending);
};
class account_item : public QObject
{
	Q_OBJECT
	Q_PROPERTY (QString balance READ getBalance NOTIFY balanceChanged)
	Q_PROPERTY (QString account READ getAccount NOTIFY accountChanged)
	Q_PROPERTY (bool isAdhoc READ isAdhoc NOTIFY isAdhocChanged)
public:
	account_item (QString balance, QString account, bool isAdhoc, QObject * parent = nullptr);
	QString getBalance ();
	QString getAccount ();
	bool isAdhoc ();

	Q_SIGNAL void balanceChanged (QString balance);
	Q_SIGNAL void accountChanged (QString account);
	Q_SIGNAL void isAdhocChanged (bool isAdhoc);

private:
	QString m_balance;
	QString m_account;
	bool m_isAdhoc;
};
class accounts : public QObject
{
	Q_OBJECT
	Q_PROPERTY (QList<QObject *> model READ getModel NOTIFY modelChanged)
	Q_PROPERTY (QString totalBalance READ getTotalBalance NOTIFY totalBalanceChanged)
	Q_PROPERTY (QString totalPending READ getTotalPending NOTIFY totalPendingChanged)
public:
	accounts (rai_qt::wallet &);
	void refresh_wallet_balance ();
	Q_INVOKABLE void refresh ();
	QWidget * window;
	QVBoxLayout * layout;
	QPushButton * import_wallet;
	QPushButton * backup_seed;
	QFrame * separator;
	QLineEdit * account_key_line;
	QPushButton * account_key_button;
	QPushButton * back;
	rai_qt::wallet & wallet;

	Q_INVOKABLE void createAccount ();
	Q_SIGNAL void createAccountSuccess ();
	Q_SIGNAL void createAccountFailure (QString msg);

	Q_INVOKABLE void useAccount (QString account);

	QList<QObject *> getModel ();
	QString getTotalBalance ();
	QString getTotalPending ();

	Q_SIGNAL void modelChanged (QList<QObject *> model);
	Q_SIGNAL void totalBalanceChanged (QString totalBalance);
	Q_SIGNAL void totalPendingChanged (QString totalPending);

private:
	QList<QObject *> m_model;
	QString m_totalBalance = "";
	QString m_totalPending = "";

	void setTotalBalance (QString totalBalance);
	void setTotalPending (QString totalPending);
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
class history_item : public QObject
{
	Q_OBJECT
	Q_PROPERTY (QString type READ getType NOTIFY typeChanged)
	Q_PROPERTY (QString account READ getAccount NOTIFY accountChanged)
	Q_PROPERTY (QString amount READ getAmount NOTIFY amountChanged)
	Q_PROPERTY (QString hash READ getHash NOTIFY hashChanged)
public:
	history_item (QString type, QString account, QString amount, QString hash, QObject * parent = nullptr);
	QString getType ();
	QString getAccount ();
	QString getAmount ();
	QString getHash ();

	Q_SIGNAL void typeChanged (QString type);
	Q_SIGNAL void accountChanged (QString account);
	Q_SIGNAL void amountChanged (QString amount);
	Q_SIGNAL void hashChanged (QString hash);

private:
	QString m_type;
	QString m_account;
	QString m_amount;
	QString m_hash;
};
class history : public QObject
{
	Q_OBJECT
	Q_PROPERTY (QList<QObject *> model READ getModel NOTIFY modelChanged)
public:
	history (rai::ledger &, rai::account const &, rai_qt::wallet &);
	void refresh ();
	rai::ledger & ledger;
	rai::account const & account;
	rai_qt::wallet & wallet;

	QList<QObject *> getModel ();

	Q_SIGNAL void modelChanged (QList<QObject *> model);

private:
	QList<QObject *> m_model;
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
class account_viewer
{
public:
	account_viewer (rai_qt::wallet &);
	QWidget * window;
	QVBoxLayout * layout;
	QLabel * account_label;
	QLineEdit * account_line;
	QPushButton * refresh;
	QWidget * balance_window;
	QHBoxLayout * balance_layout;
	QLabel * balance_label;
	rai_qt::history history;
	QPushButton * back;
	rai::account account;
	rai_qt::wallet & wallet;
};
enum class status_types
{
	not_a_status,
	disconnected,
	working,
	locked,
	vulnerable,
	active,
	synchronizing,
	nominal
};

class status : public QObject
{
	Q_OBJECT
	Q_PROPERTY (QString text READ getText NOTIFY textChanged)
	Q_PROPERTY (QColor color READ getColor NOTIFY colorChanged)
public:
	status (rai_qt::wallet &);
	void erase (rai_qt::status_types);
	void insert (rai_qt::status_types);
	std::set<rai_qt::status_types> active;
	rai_qt::wallet & wallet;

	QString getText ();
	QColor getColor ();

	Q_SIGNAL void textChanged (QString text);
	Q_SIGNAL void colorChanged (QColor color);

private:
	QString m_text;
	QColor m_color;

	QString text ();
	QColor color ();
	void set_text ();
};
class wallet : public QObject, public std::enable_shared_from_this<rai_qt::wallet>
{
	Q_OBJECT
	Q_PROPERTY (bool processingSend READ isProcessingSend NOTIFY processingSendChanged)
public:
	wallet (QApplication &, rai_qt::eventloop_processor &, rai::node &, std::shared_ptr<rai::wallet>, rai::account &);
	void start ();
	void refresh ();
	void update_connected ();
	void empty_password ();
	void change_rendering_ratio (rai::uint128_t const &);
	std::string format_balance (rai::uint128_t const &) const;
	rai::uint128_t rendering_ratio;
	rai::node & node;
	std::shared_ptr<rai::wallet> wallet_m;
	rai::account & account;
	rai_qt::eventloop_processor & processor;
	rai_qt::history history;
	rai_qt::accounts accounts;
	rai_qt::self_pane self;
	rai_qt::settings settings;
	rai_qt::advanced_actions advanced;
	rai_qt::block_creation block_creation;
	rai_qt::block_entry block_entry;
	rai_qt::block_viewer block_viewer;
	rai_qt::account_viewer account_viewer;
	rai_qt::import import;

	QApplication & application;
	QStackedWidget * main_stack;

	QWidget * client_window;
	QVBoxLayout * client_layout;

	QWidget * entry_window;
	QVBoxLayout * entry_window_layout;
	QFrame * separator;
	QPushButton * settings_button;
	QPushButton * accounts_button;
	QPushButton * show_advanced;

	rai_qt::status active_status;
	void pop_main_stack ();
	void push_main_stack (QWidget *);

	Q_INVOKABLE void send (QString amount, QString address);
	bool isProcessingSend ();

	Q_SIGNAL void sendFinished (SendResult::Type result);
	Q_SIGNAL void processingSendChanged (bool processingSend);

private:
	std::unique_ptr<QObject> m_qmlgui;
	bool m_processingSend = false;

	void setProcessingSend (bool processingSend);
};
}
