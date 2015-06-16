#include <rai/qt/qt.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <sstream>

rai_qt::self_pane::self_pane (rai_qt::wallet & wallet_a, rai::account const & account_a) :
window (new QWidget),
layout (new QVBoxLayout),
your_account_label (new QLabel ("Your RaiBlocks account:")),
account_button (new QPushButton),
balance_label (new QLabel),
wallet (wallet_a)
{
    account_button->setFlat (true);
	layout->addWidget (your_account_label);
	layout->addWidget (account_button);
	layout->addWidget (balance_label);
	layout->setContentsMargins (5, 5, 5, 5);
    window->setLayout (layout);

    QObject::connect (account_button, &QPushButton::clicked, [this] ()
    {
        wallet.application.clipboard ()->setText (account_button->text ());
    });
}

void rai_qt::self_pane::refresh_balance ()
{
	rai::transaction transaction (wallet.node.store.environment, nullptr, false);
	std::string balance;
	rai::amount (wallet.node.ledger.account_balance (transaction, wallet.account) / rai::Grai_ratio).encode_dec (balance);
	wallet.self.balance_label->setText (QString ((std::string ("Balance: ") + balance).c_str ()));
}

rai_qt::accounts::accounts (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
model (new QStandardItemModel),
view (new QTableView),
use_account (new QPushButton ("Use account")),
create_account (new QPushButton ("Create account")),
import_wallet (new QPushButton ("Import wallet")),
separator (new QFrame),
account_key_line (new QLineEdit),
account_key_button (new QPushButton ("Create custom account")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
	separator->setFrameShape (QFrame::HLine);
	separator->setFrameShadow (QFrame::Sunken);
    model->setHorizontalHeaderItem (0, new QStandardItem ("Balance"));
    model->setHorizontalHeaderItem (1, new QStandardItem ("Account"));
    view->setEditTriggers (QAbstractItemView::NoEditTriggers);
    view->setModel (model);
    view->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::ResizeMode::ResizeToContents);
    view->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::ResizeMode::Stretch);
    view->verticalHeader ()->hide ();
    view->setContextMenuPolicy (Qt::ContextMenuPolicy::CustomContextMenu);
	layout->addWidget (view);
	layout->addWidget (use_account);
	layout->addWidget (create_account);
	layout->addWidget (import_wallet);
	layout->addWidget (separator);
	layout->addWidget (account_key_line);
	layout->addWidget (account_key_button);
    layout->addWidget (back);
    window->setLayout (layout);
	QObject::connect (use_account, &QPushButton::released, [this] ()
	{
		auto selection (view->selectionModel ()->selection ().indexes ());
		if (selection.size () == 1)
		{
			auto error (wallet.account.decode_base58check (model->item (selection [0].row (), 1)->text ().toStdString ()));
			assert (!error);
			wallet.refresh ();
		}
	});
    QObject::connect (account_key_button, &QPushButton::released, [this] ()
    {
      QString key_text_wide (account_key_line->text ());
      std::string key_text (key_text_wide.toLocal8Bit ());
      rai::private_key key;
      if (!key.decode_hex (key_text))
      {
          QPalette palette;
          palette.setColor (QPalette::Text, Qt::black);
          account_key_line->setPalette (palette);
          account_key_line->clear ();
          wallet.wallet_m->insert (key);
          wallet.accounts.refresh ();
          wallet.history.refresh ();
      }
      else
      {
          QPalette palette;
          palette.setColor (QPalette::Text, Qt::red);
          account_key_line->setPalette (palette);
      }
    });
    QObject::connect (back, &QPushButton::clicked, [this] ()
    {
        wallet.pop_main_stack ();
	});
	QObject::connect (create_account, &QPushButton::released, [this] ()
	{
        rai::keypair key;
        wallet.wallet_m->insert (key.prv);
        refresh ();
	});
    QObject::connect (view, &QTableView::clicked, [this] (QModelIndex const & index_a)
    {
        auto item (model->item (index_a.row (), 1));
        assert (item != nullptr);
        wallet.application.clipboard ()->setText (item->text ());
    });
	QObject::connect (import_wallet, &QPushButton::released, [this] ()
	{
		wallet.push_main_stack (wallet.import.window);
	});
}

void rai_qt::accounts::refresh ()
{
    model->removeRows (0, model->rowCount ());
	rai::transaction transaction (wallet.wallet_m->store.environment, nullptr, false);
    for (auto i (wallet.wallet_m->store.begin (transaction)), j (wallet.wallet_m->store.end ()); i != j; ++i)
    {
        QList <QStandardItem *> items;
        rai::public_key key (i->first);
        std::string balance;
		rai::amount (wallet.node.ledger.account_balance (transaction, key) / rai::Grai_ratio).encode_dec (balance);
        items.push_back (new QStandardItem (balance.c_str ()));
        items.push_back (new QStandardItem (QString (key.to_base58check ().c_str ())));
        model->appendRow (items);
    }
}

rai_qt::import::import (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
filename_label (new QLabel ("Filename:")),
filename (new QLineEdit),
password_label (new QLabel ("Password:")),
password (new QLineEdit),
perform (new QPushButton ("Import")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
	layout->addWidget (filename_label);
	layout->addWidget (filename);
	layout->addWidget (password_label);
	layout->addWidget (password);
	layout->addWidget (perform);
	layout->addStretch ();
	layout->addWidget (back);
	window->setLayout (layout);
	QObject::connect (perform, &QPushButton::released, [this] ()
	{
		std::ifstream stream;
		stream.open (filename->text ().toStdString ().c_str ());
		if (!stream.fail ())
		{
			std::stringstream contents;
			contents << stream.rdbuf ();
			wallet.wallet_m->import (contents.str (), password->text ().toStdString ().c_str ());
		}
	});
	QObject::connect (back, &QPushButton::released, [this] ()
	{
		wallet.pop_main_stack ();
	});
}

rai_qt::history::history (rai::ledger & ledger_a, rai::account const & account_a) :
model (new QStandardItemModel),
view (new QTableView),
ledger (ledger_a),
account (account_a)
{
    model->setHorizontalHeaderItem (0, new QStandardItem ("History"));
    view->setModel (model);
    view->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::ResizeMode::Stretch);
}


namespace
{
class short_text_visitor : public rai::block_visitor
{
public:
	short_text_visitor (MDB_txn * transaction_a, rai::ledger & ledger_a) :
	transaction (transaction_a),
	ledger (ledger_a)
	{
	}
	void send_block (rai::send_block const & block_a)
	{
		auto amount (ledger.amount (transaction, block_a.hash ()));
		std::string balance;
		rai::amount (amount / rai::Grai_ratio).encode_dec (balance);
		text = boost::str (boost::format ("Sent %1%") % balance);
	}
	void receive_block (rai::receive_block const & block_a)
	{
		auto amount (ledger.amount (transaction, block_a.source ()));
		std::string balance;
		rai::amount (amount / rai::Grai_ratio).encode_dec (balance);
		text = boost::str (boost::format ("Received %1%") % balance);
	}
	void open_block (rai::open_block const & block_a)
	{
		auto amount (ledger.amount (transaction, block_a.source ()));
		std::string balance;
		rai::amount (amount / rai::Grai_ratio).encode_dec (balance);
		text = boost::str (boost::format ("Opened %1%") % balance);
	}
	void change_block (rai::change_block const & block_a)
	{
		text = boost::str (boost::format ("Changed: %1%") % block_a.hashables.representative.to_base58check ());
	}
	MDB_txn * transaction;
	rai::ledger & ledger;
	std::string text;
};
}

void rai_qt::history::refresh ()
{
	rai::transaction transaction (ledger.store.environment, nullptr, false);
	model->removeRows (0, model->rowCount ());
	auto hash (ledger.latest (transaction, account));
	short_text_visitor visitor (transaction, ledger);
	while (!hash.is_zero ())
	{
		QList <QStandardItem *> items;
		auto block (ledger.store.block_get (transaction, hash));
		assert (block != nullptr);
		block->visit (visitor);
		items.push_back (new QStandardItem (QString (visitor.text.c_str ())));
		hash = block->previous ();
		model->appendRow (items);
	}
}

rai_qt::block_viewer::block_viewer (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
hash_label (new QLabel ("Hash:")),
hash (new QLineEdit),
block_label (new QLabel ("Block:")),
block (new QPlainTextEdit),
successor_label (new QLabel ("Successor:")),
successor (new QLineEdit),
retrieve (new QPushButton ("Retrieve")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
	layout->addWidget (hash_label);
	layout->addWidget (hash);
	layout->addWidget (block_label);
	layout->addWidget (block);
	layout->addWidget (successor_label);
	layout->addWidget (successor);
	layout->addWidget (retrieve);
	layout->addStretch ();
	layout->addWidget (back);
	window->setLayout (layout);
	QObject::connect (back, &QPushButton::released, [this] ()
	{
		wallet.pop_main_stack ();
	});
	QObject::connect (retrieve, &QPushButton::released, [this] ()
	{
		rai::block_hash hash_l;
		if (!hash_l.decode_hex (hash->text ().toStdString ()))
		{
			rai::transaction transaction (wallet.node.store.environment, nullptr, false);
			auto block_l (wallet.node.store.block_get (transaction, hash_l));
			if (block_l != nullptr)
			{
				std::string contents;
				block_l->serialize_json (contents);
				block->setPlainText (contents.c_str ());
				auto successor_l (wallet.node.store.block_successor (transaction, hash_l));
				successor->setText (successor_l.to_string ().c_str ());
			}
			else
			{
				block->setPlainText ("Block not found");
			}
		}
		else
		{
			block->setPlainText ("Bad block hash");
		}
	});
}

rai_qt::wallet::wallet (QApplication & application_a, rai::node & node_a, std::shared_ptr <rai::wallet> wallet_a, rai::account const & account_a) :
node (node_a),
wallet_m (wallet_a),
account (account_a),
history (node.ledger, account_a),
accounts (*this),
self (*this, account_a),
password_change (*this),
enter_password (*this),
advanced (*this),
block_creation (*this),
block_entry (*this),
block_viewer (*this),
import (*this),
application (application_a),
status (new QLabel ("Status: Disconnected")),
main_stack (new QStackedWidget),
client_window (new QWidget),
client_layout (new QVBoxLayout),
entry_window (new QWidget),
entry_window_layout (new QVBoxLayout),
account_history_label (new QLabel ("Account history:")),
send_blocks (new QPushButton ("Send")),
show_advanced (new QPushButton ("Advanced")),
send_blocks_window (new QWidget),
send_blocks_layout (new QVBoxLayout),
send_account_label (new QLabel ("Destination account:")),
send_account (new QLineEdit),
send_count_label (new QLabel ("Amount:")),
send_count (new QLineEdit),
send_blocks_send (new QPushButton ("Send")),
send_blocks_back (new QPushButton ("Back")),
last_status (rai_qt::status::disconnected)
{
    send_blocks_layout->addWidget (send_account_label);
    send_blocks_layout->addWidget (send_account);
    send_blocks_layout->addWidget (send_count_label);
    send_blocks_layout->addWidget (send_count);
    send_blocks_layout->addWidget (send_blocks_send);
    send_blocks_layout->addStretch ();
    send_blocks_layout->addWidget (send_blocks_back);
    send_blocks_layout->setContentsMargins (0, 0, 0, 0);
    send_blocks_window->setLayout (send_blocks_layout);
	
	entry_window_layout->addWidget (account_history_label);
	entry_window_layout->addWidget (history.view);
    entry_window_layout->addWidget (send_blocks);
    entry_window_layout->addWidget (show_advanced);
    entry_window_layout->setContentsMargins (0, 0, 0, 0);
    entry_window_layout->setSpacing (5);
    entry_window->setLayout (entry_window_layout);

    main_stack->addWidget (entry_window);

	status->setAlignment (Qt::AlignHCenter);
	
	client_layout->addWidget (status);
    client_layout->addWidget (self.window);
    client_layout->addWidget (main_stack);
    client_layout->setSpacing (0);
    client_layout->setContentsMargins (0, 0, 0, 0);
    client_window->setLayout (client_layout);
    client_window->resize (320, 480);

    QObject::connect (show_advanced, &QPushButton::released, [this] ()
    {
        push_main_stack (advanced.window);
    });
    QObject::connect (send_blocks_send, &QPushButton::released, [this] ()
    {
        QString coins_text (send_count->text ());
        std::string coins_text_narrow (coins_text.toLocal8Bit ());
        try
        {
            rai::amount amount (coins_text_narrow);
            rai::uint128_t coins (amount.number () * rai::Grai_ratio);
            if (coins / rai::Grai_ratio == amount.number ())
            {
                QPalette palette;
                palette.setColor (QPalette::Text, Qt::black);
                send_count->setPalette (palette);
                QString account_text (send_account->text ());
                std::string account_text_narrow (account_text.toLocal8Bit ());
                rai::account account_l;
                auto parse_error (account_l.decode_base58check (account_text_narrow));
                if (!parse_error)
                {
                    auto send_error (wallet_m->send (account, account_l, coins));
                    if (!send_error)
                    {
                        QPalette palette;
                        palette.setColor (QPalette::Text, Qt::black);
                        send_account->setPalette (palette);
                        send_count->clear ();
                        send_account->clear ();
                        accounts.refresh ();
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
                    send_account->setPalette (palette);
                }
            }
            else
            {
                QPalette palette;
                palette.setColor (QPalette::Text, Qt::red);
                send_account->setPalette (palette);
            }
        }
        catch (...)
        {
            QPalette palette;
            palette.setColor (QPalette::Text, Qt::red);
            send_count->setPalette (palette);
        }
    });
    QObject::connect (send_blocks_back, &QPushButton::released, [this] ()
    {
        pop_main_stack ();
    });
    QObject::connect (send_blocks, &QPushButton::released, [this] ()
    {
        push_main_stack (send_blocks_window);
    });
    node.observers.push_back ([this] (rai::block const &, rai::account const & account_a)
    {
		QTimer::singleShot(0, [this, account_a] ()
		{
			if (wallet_m->exists (account_a))
			{
				accounts.refresh ();
			}
			if (account_a == account)
			{
				history.refresh ();
				self.refresh_balance ();
			}
		});
    });
	node.endpoint_observers.push_back ([this] (rai::endpoint const &)
	{
		if (last_status == rai_qt::status::disconnected)
		{
			last_status = rai_qt::status::connected;
			status->setText ("Status: Connected");
		}
	});
	node.disconnect_observers.push_back ([this] ()
	{
		if (last_status == rai_qt::status::connected)
		{
			last_status = rai_qt::status::disconnected;
			status->setText ("Status: Disconnected");
		}
	});
	refresh ();
}

void rai_qt::wallet::refresh ()
{
	{
		rai::transaction transaction (wallet_m->store.environment, nullptr, false);
		assert (wallet_m->store.exists (transaction, account));
	}
    self.account_button->setText (QString (account.to_base58check ().c_str ()));
	self.refresh_balance ();
    accounts.refresh ();
    history.refresh ();
}

void rai_qt::wallet::push_main_stack (QWidget * widget_a)
{
    main_stack->addWidget (widget_a);
    main_stack->setCurrentIndex (main_stack->count () - 1);
}

void rai_qt::wallet::pop_main_stack ()
{
    main_stack->removeWidget (main_stack->currentWidget ());
}

void rai_qt::password_change::clear ()
{
    password_label->setStyleSheet ("QLabel { color: black }");
    password->clear ();
    retype_label->setStyleSheet ("QLabel { color: black }");
    retype->clear ();
}

rai_qt::password_change::password_change (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
password_label (new QLabel ("New password:")),
password (new QLineEdit),
retype_label (new QLabel ("Retype password:")),
retype (new QLineEdit),
change (new QPushButton ("Change password")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
    password->setEchoMode (QLineEdit::EchoMode::Password);
    layout->addWidget (password_label);
    layout->addWidget (password);
    retype->setEchoMode (QLineEdit::EchoMode::Password);
    layout->addWidget (retype_label);
    layout->addWidget (retype);
    layout->addWidget (change);
    layout->addStretch ();
    layout->addWidget (back);
    layout->setContentsMargins (0, 0, 0, 0);
    window->setLayout (layout);
    QObject::connect (change, &QPushButton::released, [this] ()
    {
		rai::transaction transaction (wallet.wallet_m->store.environment, nullptr, true);
        if (wallet.wallet_m->store.valid_password (transaction))
        {
            if (password->text () == retype->text ())
            {
                wallet.wallet_m->store.rekey (transaction, std::string (password->text ().toLocal8Bit ()));
                clear ();
            }
            else
            {
                password_label->setStyleSheet ("QLabel { color: red }");
                retype_label->setStyleSheet ("QLabel { color: red }");
            }
        }
    });
    QObject::connect (back, &QPushButton::released, [this] ()
    {
        clear ();
        wallet.pop_main_stack ();
    });
}

rai_qt::enter_password::enter_password (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
valid (new QLabel),
password (new QLineEdit),
unlock (new QPushButton ("Unlock")),
lock (new QPushButton ("Lock")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
    password->setEchoMode (QLineEdit::EchoMode::Password);
    layout->addWidget (valid);
    layout->addWidget (password);
    layout->addWidget (unlock);
    layout->addWidget (lock);
    layout->addStretch ();
    layout->addWidget (back);
    window->setLayout (layout);
    QObject::connect (back, &QPushButton::released, [this] ()
    {
        assert (wallet.main_stack->currentWidget () == window);
        wallet.pop_main_stack ();
    });
    QObject::connect (unlock, &QPushButton::released, [this] ()
    {
		{
			rai::transaction transaction (wallet.wallet_m->store.environment, nullptr, false);
			wallet.wallet_m->store.enter_password (transaction, std::string (password->text ().toLocal8Bit ()));
		}
        update_label ();
    });
    QObject::connect (lock, &QPushButton::released, [this] ()
    {
        rai::uint256_union empty;
        empty.clear ();
        wallet.wallet_m->store.password.value_set (empty);
        update_label ();
    });
}

void rai_qt::enter_password::activate ()
{
    wallet.push_main_stack (window);
    update_label ();
}

void rai_qt::enter_password::update_label ()
{
	rai::transaction transaction (wallet.wallet_m->store.environment, nullptr, false);
    if (wallet.wallet_m->store.valid_password (transaction))
    {
        valid->setStyleSheet ("QLabel { color: black }");
        valid->setText ("Password: Valid");
        password->setText ("");
    }
    else
    {
        valid->setStyleSheet ("QLabel { color: red }");
        valid->setText ("Password: INVALID");
    }
}

rai_qt::advanced_actions::advanced_actions (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
enter_password (new QPushButton ("Enter Password")),
change_password (new QPushButton ("Change Password")),
accounts (new QPushButton ("Accounts")),
show_ledger (new QPushButton ("Ledger")),
show_peers (new QPushButton ("Peers")),
search_for_receivables (new QPushButton ("Search for receivables")),
wallet_refresh (new QPushButton ("Refresh Wallet")),
create_block (new QPushButton ("Create Block")),
enter_block (new QPushButton ("Enter Block")),
block_viewer (new QPushButton ("Block Viewer")),
back (new QPushButton ("Back")),
ledger_window (new QWidget),
ledger_layout (new QVBoxLayout),
ledger_model (new QStandardItemModel),
ledger_view (new QTableView),
ledger_refresh (new QPushButton ("Refresh")),
ledger_back (new QPushButton ("Back")),
peers_window (new QWidget),
peers_layout (new QVBoxLayout),
peers_model (new QStringListModel),
peers_view (new QListView),
peers_refresh (new QPushButton ("Refresh")),
peers_back (new QPushButton ("Back")),
wallet (wallet_a)
{
    ledger_model->setHorizontalHeaderItem (0, new QStandardItem ("Account"));
    ledger_model->setHorizontalHeaderItem (1, new QStandardItem ("Balance"));
    ledger_model->setHorizontalHeaderItem (2, new QStandardItem ("Block"));
    ledger_view->setModel (ledger_model);
    ledger_view->setEditTriggers (QAbstractItemView::NoEditTriggers);
    ledger_view->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::ResizeMode::Stretch);
    ledger_view->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::ResizeMode::ResizeToContents);
    ledger_view->horizontalHeader ()->setSectionResizeMode (2, QHeaderView::ResizeMode::Stretch);
    ledger_view->verticalHeader ()->hide ();
    ledger_layout->addWidget (ledger_view);
    ledger_layout->addWidget (ledger_refresh);
    ledger_layout->addWidget (ledger_back);
    ledger_layout->setContentsMargins (0, 0, 0, 0);
    ledger_window->setLayout (ledger_layout);
    
    peers_view->setEditTriggers (QAbstractItemView::NoEditTriggers);
    peers_view->setModel (peers_model);
    peers_layout->addWidget (peers_view);
    peers_layout->addWidget (peers_refresh);
    peers_layout->addWidget (peers_back);
    peers_layout->setContentsMargins (0, 0, 0, 0);
    peers_window->setLayout (peers_layout);

    layout->addWidget (enter_password);
    layout->addWidget (change_password);
    layout->addWidget (accounts);
    layout->addWidget (show_ledger);
    layout->addWidget (show_peers);
    layout->addWidget (search_for_receivables);
    layout->addWidget (wallet_refresh);
    layout->addWidget (create_block);
    layout->addWidget (enter_block);
	layout->addWidget (block_viewer);
    layout->addStretch ();
    layout->addWidget (back);
    window->setLayout (layout);

    QObject::connect (accounts, &QPushButton::released, [this] ()
    {
        wallet.push_main_stack (wallet.accounts.window);
    });
    QObject::connect (enter_password, &QPushButton::released, [this] ()
    {
        wallet.enter_password.activate ();
    });
    QObject::connect (change_password, &QPushButton::released, [this] ()
    {
        wallet.push_main_stack (wallet.password_change.window);
    });
    QObject::connect (wallet_refresh, &QPushButton::released, [this] ()
    {
        wallet.accounts.refresh ();
    });
    QObject::connect (show_peers, &QPushButton::released, [this] ()
    {
        wallet.push_main_stack (peers_window);
    });
    QObject::connect (show_ledger, &QPushButton::released, [this] ()
    {
        wallet.push_main_stack (ledger_window);
    });
    QObject::connect (back, &QPushButton::released, [this] ()
    {
        wallet.pop_main_stack ();
    });
    QObject::connect (peers_back, &QPushButton::released, [this] ()
    {
        wallet.pop_main_stack ();
    });
    QObject::connect (peers_refresh, &QPushButton::released, [this] ()
    {
        refresh_peers ();
    });
    QObject::connect (ledger_refresh, &QPushButton::released, [this] ()
    {
        refresh_ledger ();
    });
    QObject::connect (ledger_back, &QPushButton::released, [this] ()
    {
        wallet.pop_main_stack ();
    });
    QObject::connect (search_for_receivables, &QPushButton::released, [this] ()
    {
		auto node (wallet.node.shared ());
        node->background ([node] () {node->search_pending ();});
    });
    QObject::connect (create_block, &QPushButton::released, [this] ()
    {
        wallet.push_main_stack (wallet.block_creation.window);
    });
    QObject::connect (enter_block, &QPushButton::released, [this] ()
    {
        wallet.push_main_stack (wallet.block_entry.window);
    });
	QObject::connect (block_viewer, &QPushButton::released, [this] ()
	{
		wallet.push_main_stack (wallet.block_viewer.window);
	});
    refresh_ledger ();
}

void rai_qt::advanced_actions::refresh_peers ()
{
    QStringList peers;
    for (auto i: wallet.node.peers.list ())
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

void rai_qt::advanced_actions::refresh_ledger ()
{
    ledger_model->removeRows (0, ledger_model->rowCount ());
	rai::transaction transaction (wallet.node.store.environment, nullptr, false);
    for (auto i (wallet.node.ledger.store.latest_begin (transaction)), j (wallet.node.ledger.store.latest_end ()); i != j; ++i)
    {
        QList <QStandardItem *> items;
        items.push_back (new QStandardItem (QString (rai::block_hash (i->first).to_base58check ().c_str ())));
		auto hash (rai::account_info (i->second).head);
		std::string balance;
		rai::amount (wallet.node.ledger.balance (transaction, hash) / rai::Grai_ratio).encode_dec (balance);
        items.push_back (new QStandardItem (QString (balance.c_str ())));
        std::string block_hash;
        hash.encode_hex (block_hash);
        items.push_back (new QStandardItem (QString (block_hash.c_str ())));
        ledger_model->appendRow (items);
    }
}

rai_qt::block_entry::block_entry (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
block (new QPlainTextEdit),
status (new QLabel),
process (new QPushButton ("Process")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
    layout->addWidget (block);
    layout->addWidget (status);
    layout->addWidget (process);
    layout->addWidget (back);
    window->setLayout (layout);
    QObject::connect (process, &QPushButton::released, [this] ()
    {
        auto string (block->toPlainText ().toStdString ());
        try
        {
            boost::property_tree::ptree tree;
            std::stringstream istream (string);
            boost::property_tree::read_json (istream, tree);
            auto block_l (rai::deserialize_block_json (tree));
            if (block_l != nullptr)
            {
                wallet.node.process_receive_republish (std::move (block_l));
            }
            else
            {
                status->setStyleSheet ("QLabel { color: red }");
                status->setText ("Unable to parse block");
            }
        }
        catch (std::runtime_error const &)
        {
            status->setStyleSheet ("QLabel { color: red }");
            status->setText ("Unable to parse block");
        }
    });
    QObject::connect (back, &QPushButton::released, [this] ()
    {
        wallet.pop_main_stack ();
    });
}

rai_qt::block_creation::block_creation (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
group (new QButtonGroup),
button_layout (new QHBoxLayout),
send (new QRadioButton ("Send")),
receive (new QRadioButton ("Receive")),
change (new QRadioButton ("Change")),
open (new QRadioButton ("Open")),
account_label (new QLabel ("Account:")),
account (new QLineEdit),
source_label (new QLabel ("Source:")),
source (new QLineEdit),
amount_label (new QLabel ("Amount:")),
amount (new QLineEdit),
destination_label (new QLabel ("Destination:")),
destination (new QLineEdit),
representative_label (new QLabel ("Representative:")),
representative (new QLineEdit),
block (new QPlainTextEdit),
status (new QLabel),
create (new QPushButton ("Create")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
    group->addButton (send);
    group->addButton (receive);
    group->addButton (change);
    group->addButton (open);
    group->setId (send, 0);
    group->setId (receive, 1);
    group->setId (change, 2);
    group->setId (open, 3);
    
    button_layout->addWidget (send);
    button_layout->addWidget (receive);
    button_layout->addWidget (open);
    button_layout->addWidget (change);
    
    layout->addLayout (button_layout);
    layout->addWidget (account_label);
    layout->addWidget (account);
    layout->addWidget (source_label);
    layout->addWidget (source);
    layout->addWidget (amount_label);
    layout->addWidget (amount);
    layout->addWidget (destination_label);
    layout->addWidget (destination);
    layout->addWidget (representative_label);
    layout->addWidget (representative);
    layout->addWidget (block);
    layout->addWidget (status);
    layout->addWidget (create);
    layout->addWidget (back);
    window->setLayout (layout);
    QObject::connect (send, &QRadioButton::toggled, [this] ()
    {
        if (send->isChecked ())
        {
            deactivate_all ();
            activate_send ();
        }
    });
    QObject::connect (receive, &QRadioButton::toggled, [this] ()
    {
        if (receive->isChecked ())
        {
            deactivate_all ();
            activate_receive ();
        }
    });
    QObject::connect (open, &QRadioButton::toggled, [this] ()
    {
        if (open->isChecked ())
        {
            deactivate_all ();
            activate_open ();
        }
    });
    QObject::connect (change, &QRadioButton::toggled, [this] ()
    {
        if (change->isChecked ())
        {
            deactivate_all ();
            activate_change ();
        }
    });
    QObject::connect (create, &QPushButton::released, [this] ()
    {
        switch (group->checkedId ())
        {
            case 0:
                create_send ();
                break;
            case 1:
                create_receive ();
                break;
            case 2:
                create_change ();
                break;
            case 3:
                create_open ();
                break;
            default:
                assert (false);
                break;
        }
    });
    QObject::connect (back, &QPushButton::released, [this] ()
    {
        wallet.pop_main_stack ();
    });
    send->click ();
}

void rai_qt::block_creation::deactivate_all ()
{
    account_label->hide ();
    account->hide ();
    source_label->hide ();
    source->hide ();
    amount_label->hide ();
    amount->hide ();
    destination_label->hide ();
    destination->hide ();
    representative_label->hide ();
    representative->hide ();
}

void rai_qt::block_creation::activate_send ()
{
    account_label->show ();
    account->show ();
    amount_label->show ();
    amount->show ();
    destination_label->show ();
    destination->show ();
}

void rai_qt::block_creation::activate_receive ()
{
    source_label->show ();
    source->show ();
}

void rai_qt::block_creation::activate_open ()
{
    source_label->show ();
    source->show ();
    representative_label->show ();
    representative->show ();
}

void rai_qt::block_creation::activate_change ()
{
    account_label->show ();
    account->show ();
    representative_label->show ();
    representative->show ();
}

void rai_qt::block_creation::create_send ()
{
    rai::account account_l;
    auto error (account_l.decode_base58check (account->text ().toStdString ()));
    if (!error)
    {
        rai::amount amount_l;
        error = amount_l.decode_hex (amount->text ().toStdString ());
        if (!error)
        {
            rai::account destination_l;
            error = destination_l.decode_base58check (destination->text ().toStdString ());
            if (!error)
            {
				rai::transaction transaction (wallet.node.store.environment, nullptr, false);
                rai::private_key key;
                if (!wallet.wallet_m->store.fetch (transaction, account_l, key))
                {
                    auto balance (wallet.node.ledger.account_balance (transaction, account_l));
                    if (amount_l.number () <= balance)
                    {
                        rai::account_info info;
                        auto error (wallet.node.store.account_get (transaction, account_l, info));
                        assert (!error);
                        rai::send_block send (info.head, destination_l, balance - amount_l.number (), key, account_l, wallet.wallet_m->work_fetch (transaction, account_l, info.head));
                        key.clear ();
                        std::string block_l;
                        send.serialize_json (block_l);
                        block->setPlainText (QString (block_l.c_str ()));
                        status->setStyleSheet ("QLabel { color: black }");
                        status->setText ("Created block");
                    }
                    else
                    {
                        status->setStyleSheet ("QLabel { color: red }");
                        status->setText ("Insufficient balance");
                    }
                }
                else
                {
                    status->setStyleSheet ("QLabel { color: red }");
                    status->setText ("Account is not in wallet");
                }
            }
            else
            {
                status->setStyleSheet ("QLabel { color: red }");
                status->setText ("Unable to decode destination");
            }
        }
        else
        {
            status->setStyleSheet ("QLabel { color: red }");
            status->setText ("Unable to decode amount");
        }
    }
    else
    {
        status->setStyleSheet ("QLabel { color: red }");
        status->setText ("Unable to decode account");
    }
}

void rai_qt::block_creation::create_receive ()
{
    rai::block_hash source_l;
    auto error (source_l.decode_hex (source->text ().toStdString ()));
    if (!error)
    {
		rai::transaction transaction (wallet.node.store.environment, nullptr, false);
        rai::receivable receivable;
        if (!wallet.node.store.pending_get (transaction, source_l, receivable))
        {
            rai::account_info info;
            auto error (wallet.node.store.account_get (transaction, receivable.destination, info));
            if (!error)
            {
                rai::private_key key;
                auto error (wallet.wallet_m->store.fetch (transaction, receivable.destination, key));
                if (!error)
                {
                    rai::receive_block receive (info.head, source_l, key, receivable.destination, wallet.wallet_m->work_fetch (transaction, receivable.destination, info.head));
                    key.clear ();
                    std::string block_l;
                    receive.serialize_json (block_l);
                    block->setPlainText (QString (block_l.c_str ()));
                    status->setStyleSheet ("QLabel { color: black }");
                    status->setText ("Created block");
                }
                else
                {
                    status->setStyleSheet ("QLabel { color: red }");
                    status->setText ("Account is not in wallet");
                }
            }
            else
            {
                status->setStyleSheet ("QLabel { color: red }");
                status->setText ("Account not yet open");
            }
        }
        else
        {
            status->setStyleSheet ("QLabel { color: red }");
            status->setText ("Source block is not pending to receive");
        }
    }
    else
    {
        status->setStyleSheet ("QLabel { color: red }");
        status->setText ("Unable to decode source");
    }
}

void rai_qt::block_creation::create_change ()
{
    rai::account account_l;
    auto error (account_l.decode_base58check (account->text ().toStdString ()));
    if (!error)
    {
        rai::account representative_l;
        error = representative_l.decode_base58check (representative->text ().toStdString ());
        if (!error)
        {
			rai::transaction transaction (wallet.node.store.environment, nullptr, false);
            rai::account_info info;
            auto error (wallet.node.store.account_get (transaction, account_l, info));
            if (!error)
            {
                rai::private_key key;
                auto error (wallet.wallet_m->store.fetch (transaction, account_l, key));
                if (!error)
                {
                    rai::change_block change (info.head, representative_l, key, account_l, wallet.wallet_m->work_fetch (transaction, account_l, info.head));
                    key.clear ();
                    std::string block_l;
                    change.serialize_json (block_l);
                    block->setPlainText (QString (block_l.c_str ()));
                    status->setStyleSheet ("QLabel { color: black }");
                    status->setText ("Created block");
                }
                else
                {
                    status->setStyleSheet ("QLabel { color: red }");
                    status->setText ("Account is not in wallet");
                }
            }
            else
            {
                status->setStyleSheet ("QLabel { color: red }");
                status->setText ("Account not yet open");
            }
        }
        else
        {
            status->setStyleSheet ("QLabel { color: red }");
            status->setText ("Unable to decode representative");
        }
    }
    else
    {
        status->setStyleSheet ("QLabel { color: red }");
        status->setText ("Unable to decode account");
    }
}

void rai_qt::block_creation::create_open ()
{
    rai::block_hash source_l;
    auto error (source_l.decode_hex (source->text ().toStdString ()));
    if (!error)
    {
        rai::account representative_l;
        error = representative_l.decode_base58check (representative->text ().toStdString ());
        if (!error)
        {
			rai::transaction transaction (wallet.node.store.environment, nullptr, false);
            rai::receivable receivable;
            if (!wallet.node.store.pending_get (transaction, source_l, receivable))
            {
                rai::account_info info;
                auto error (wallet.node.store.account_get (transaction, receivable.destination, info));
                if (error)
                {
                    rai::private_key key;
                    auto error (wallet.wallet_m->store.fetch (transaction, receivable.destination, key));
                    if (!error)
                    {
                        rai::open_block open (source_l, representative_l, receivable.destination, key, receivable.destination, wallet.wallet_m->work_fetch (transaction, receivable.destination, receivable.destination));
                        key.clear ();
                        std::string block_l;
                        open.serialize_json (block_l);
                        block->setPlainText (QString (block_l.c_str ()));
                        status->setStyleSheet ("QLabel { color: black }");
                        status->setText ("Created block");
                    }
                    else
                    {
                        status->setStyleSheet ("QLabel { color: red }");
                        status->setText ("Account is not in wallet");
                    }
                }
                else
                {
                    status->setStyleSheet ("QLabel { color: red }");
                    status->setText ("Account already open");
                }
            }
            else
            {
                status->setStyleSheet ("QLabel { color: red }");
                status->setText ("Source block is not pending to receive");
            }
        }
        else
        {
            status->setStyleSheet ("QLabel { color: red }");
            status->setText ("Unable to decode representative");
        }
    }
    else
    {
        status->setStyleSheet ("QLabel { color: red }");
        status->setText ("Unable to decode source");
    }
}