#include <rai/qt/qt.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <sstream>

namespace
{
void show_line_error (QLineEdit & line)
{
	line.setStyleSheet ("QLineEdit { color: red }");
}
void show_line_ok (QLineEdit & line)
{
	line.setStyleSheet ("QLineEdit { color: black }");
}
void show_label_error (QLabel & label)
{
	label.setStyleSheet ("QLabel { color: red }");
}
void show_label_ok (QLabel & label)
{
	label.setStyleSheet ("QLabel { color: black }");
}
}

bool rai_qt::eventloop_processor::event (QEvent * event_a)
{
	assert(dynamic_cast <rai_qt::eventloop_event *>(event_a) != nullptr);
	static_cast <rai_qt::eventloop_event *> (event_a)->action ();
	return true;
}

rai_qt::eventloop_event::eventloop_event (std::function <void ()> const & action_a) :
QEvent (QEvent::Type::User),
action (action_a)
{
}

rai_qt::self_pane::self_pane (rai_qt::wallet & wallet_a, rai::account const & account_a) :
window (new QWidget),
layout (new QVBoxLayout),
self_layout (new QHBoxLayout),
self_window (new QWidget),
your_account_label (new QLabel ("Your RaiBlocks account:")),
account_window (new QWidget),
account_layout (new QHBoxLayout),
account_text (new QLabel),
copy_button (new QPushButton ("Copy")),
balance_window (new QWidget),
balance_layout (new QHBoxLayout),
balance_label (new QLabel),
ratio_group (new QButtonGroup),
mrai (new QRadioButton ("Mrai")),
krai (new QRadioButton ("krai")),
rai (new QRadioButton ("rai")),
wallet (wallet_a)
{
    ratio_group->addButton (mrai);
    ratio_group->addButton (krai);
    ratio_group->addButton (rai);
    ratio_group->setId (mrai, 0);
    ratio_group->setId (krai, 1);
    ratio_group->setId (rai, 2);

	version = new QLabel (boost::str (boost::format ("Version %1%.%2%.%3%") % RAIBLOCKS_VERSION_MAJOR % RAIBLOCKS_VERSION_MINOR % RAIBLOCKS_VERSION_PATCH).c_str ());
	self_layout->addWidget (your_account_label);
	self_layout->addStretch ();
	self_layout->addWidget (version);
	self_layout->setContentsMargins (0, 0, 0, 0);
	self_window->setLayout (self_layout);

	auto font (QFontDatabase::systemFont (QFontDatabase::FixedFont));
	font.setPointSize (account_text->font().pointSize());
	account_text->setFont (font);
	account_layout->addWidget (account_text);
	account_layout->addWidget (copy_button);
	account_layout->setContentsMargins (0, 0, 0, 0);
	account_window->setLayout (account_layout);
	layout->addWidget (self_window);
	layout->addWidget (account_window);
	balance_layout->addWidget (balance_label);
	balance_layout->addStretch ();
	balance_layout->addWidget (mrai);
	balance_layout->addWidget (krai);
	balance_layout->addWidget (rai);
	balance_layout->setContentsMargins (0, 0, 0, 0);
	balance_window->setLayout (balance_layout);
	layout->addWidget (balance_window);
	layout->setContentsMargins (5, 5, 5, 5);
	window->setLayout (layout);

	QObject::connect (copy_button, &QPushButton::clicked, [this] ()
	{
		this->wallet.application.clipboard ()->setText (QString (this->wallet.account.to_account ().c_str ()));
	});	
    QObject::connect (mrai, &QRadioButton::toggled, [this] ()
    {
        if (mrai->isChecked ())
        {
			this->wallet.change_rendering_ratio (rai::Mrai_ratio);
        }
    });	
    QObject::connect (krai, &QRadioButton::toggled, [this] ()
    {
        if (krai->isChecked ())
        {
			this->wallet.change_rendering_ratio (rai::krai_ratio);
        }
    });	
    QObject::connect (rai, &QRadioButton::toggled, [this] ()
    {
        if (rai->isChecked ())
        {
			this->wallet.change_rendering_ratio (rai::rai_ratio);
        }
    });
	mrai->click ();
}

void rai_qt::self_pane::refresh_balance ()
{
	auto balance (wallet.node.balance_pending (wallet.account));
	auto final_text (std::string ("Balance: ") + (balance.first / wallet.rendering_ratio).convert_to <std::string> ());
	if (!balance.second.is_zero ())
	{
		final_text += "\nPending: " + (balance.second / wallet.rendering_ratio).convert_to <std::string> ();
	}
	wallet.self.balance_label->setText (QString (final_text.c_str ()));
}

rai_qt::accounts::accounts (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
model (new QStandardItemModel),
view (new QTableView),
use_account (new QPushButton ("Use account")),
create_account (new QPushButton ("Create account")),
import_wallet (new QPushButton ("Import wallet")),
backup_seed (new QPushButton ("Backup/Clipboard wallet seed")),
separator (new QFrame),
account_key_line (new QLineEdit),
account_key_button (new QPushButton ("Import adhoc key")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
	separator->setFrameShape (QFrame::HLine);
	separator->setFrameShadow (QFrame::Sunken);
    model->setHorizontalHeaderItem (0, new QStandardItem ("Balance"));
    model->setHorizontalHeaderItem (1, new QStandardItem ("Account"));
    view->setEditTriggers (QAbstractItemView::NoEditTriggers);
    view->setModel (model);
    view->verticalHeader ()->hide ();
    view->setContextMenuPolicy (Qt::ContextMenuPolicy::CustomContextMenu);
	layout->addWidget (view);
	layout->addWidget (use_account);
	layout->addWidget (create_account);
	layout->addWidget (import_wallet);
	layout->addWidget (backup_seed);
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
			auto error (this->wallet.account.decode_account (model->item (selection [0].row (), 1)->text ().toStdString ()));
			assert (!error);
			this->wallet.refresh ();
		}
	});
	QObject::connect (account_key_button, &QPushButton::released, [this] ()
	{
		QString key_text_wide (account_key_line->text ());
		std::string key_text (key_text_wide.toLocal8Bit ());
		rai::raw_key key;
		if (!key.data.decode_hex (key_text))
		{
			show_line_ok (*account_key_line);
			account_key_line->clear ();
			this->wallet.wallet_m->insert_adhoc (key);
			this->wallet.accounts.refresh ();
			this->wallet.history.refresh ();
		}
		else
		{
			show_line_error (*account_key_line);
		}
	});
    QObject::connect (back, &QPushButton::clicked, [this] ()
    {
		this->wallet.pop_main_stack ();
	});
	QObject::connect (create_account, &QPushButton::released, [this] ()
	{
		this->wallet.wallet_m->deterministic_insert ();
        refresh ();
	});
	QObject::connect (import_wallet, &QPushButton::released, [this] ()
	{
		this->wallet.push_main_stack (this->wallet.import.window);
	});
	QObject::connect (backup_seed, &QPushButton::released, [this] ()
	{
		rai::raw_key seed;
		rai::transaction transaction (this->wallet.wallet_m->store.environment, nullptr, false);
		if (this->wallet.wallet_m->store.valid_password (transaction))
		{
			this->wallet.wallet_m->store.seed (seed, transaction);
			this->wallet.application.clipboard ()->setText (QString (seed.data.to_string ().c_str ()));
		}
		else
		{
			this->wallet.application.clipboard ()->setText ("");
		}
	});
}

void rai_qt::accounts::refresh ()
{
    model->removeRows (0, model->rowCount ());
	rai::transaction transaction (wallet.wallet_m->store.environment, nullptr, false);
	QBrush brush;
    for (auto i (wallet.wallet_m->store.begin (transaction)), j (wallet.wallet_m->store.end ()); i != j; ++i)
    {
        rai::public_key key (i->first);
		auto balance_amount (wallet.node.ledger.account_balance (transaction, key));
		bool display (true);
		switch (wallet.wallet_m->store.key_type (i->second))
		{
		case rai::key_type::adhoc:
		{
			brush.setColor ("red");
			display = !balance_amount.is_zero ();
			break;
		}
		default:
		{
			brush.setColor ("black");
			break;
		}
		}
		if (display)
		{
			QList <QStandardItem *> items;
			std::string balance;
			rai::amount (balance_amount / wallet.rendering_ratio).encode_dec (balance);
			items.push_back (new QStandardItem (balance.c_str ()));
			auto account (new QStandardItem (QString (key.to_account ().c_str ())));
			account->setForeground (brush);
			items.push_back (account);
			model->appendRow (items);
		}
    }
}

rai_qt::import::import (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
seed_label (new QLabel ("Seed:")),
seed (new QLineEdit),
clear_label (new QLabel ("Modifying seed clears existing keys\nType 'clear keys' below to confirm:")),
clear_line (new QLineEdit),
import_seed (new QPushButton ("Import seed")),
separator (new QFrame),
filename_label (new QLabel ("Filename:")),
filename (new QLineEdit),
password_label (new QLabel ("Password:")),
password (new QLineEdit),
perform (new QPushButton ("Import")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
	layout->addWidget (seed_label);
	layout->addWidget (seed);
	layout->addWidget (clear_label);
	layout->addWidget (clear_line);
	layout->addWidget (import_seed);
	layout->addWidget (separator);
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
			show_line_ok (*filename);
			std::stringstream contents;
			contents << stream.rdbuf ();
			if (!this->wallet.wallet_m->import (contents.str (), password->text ().toStdString ().c_str ()))
			{
				show_line_ok (*password);
				this->wallet.accounts.refresh ();
				password->clear ();
				filename->clear ();
			}
			else
			{
				show_line_error (*password);
			}
		}
		else
		{
			show_line_error (*filename);
		}
	});
	QObject::connect (back, &QPushButton::released, [this] ()
	{
		this->wallet.pop_main_stack ();
	});
	QObject::connect (import_seed, &QPushButton::released, [this] ()
	{
		if (clear_line->text ().toStdString () == "clear keys")
		{
			show_line_ok (*clear_line);
			rai::raw_key seed_l;
			if (!seed_l.data.decode_hex (seed->text ().toStdString ()))
			{
				bool successful (false);
				{
					rai::transaction transaction (this->wallet.wallet_m->store.environment, nullptr, true);
					if (this->wallet.wallet_m->store.valid_password (transaction))
					{
						this->wallet.wallet_m->store.seed_set (transaction, seed_l);
						successful = true;
					}
					else
					{
						show_line_error (*seed);
					}
				}
				if (successful)
				{
					this->wallet.account = this->wallet.wallet_m->deterministic_insert ();
					seed->clear ();
					clear_line->clear ();
					show_line_ok (*seed);
					this->wallet.refresh ();
				}
			}
			else
			{
				show_line_error (*seed);
			}
		}
		else
		{
			show_line_error (*clear_line);
		}
	});
}

rai_qt::history::history (rai::ledger & ledger_a, rai::account const & account_a, rai::uint128_t const & rendering_ratio_a) :
window (new QWidget),
layout (new QVBoxLayout),
model (new QStandardItemModel),
view (new QTableView),
tx_window (new QWidget),
tx_layout (new QHBoxLayout),
tx_label (new QLabel ("Account history count:")),
tx_count (new QSpinBox),
ledger (ledger_a),
account (account_a),
rendering_ratio (rendering_ratio_a)
{/*
	tx_count->setRange (1, 256);
	tx_layout->addWidget (tx_label);
	tx_layout->addWidget (tx_count);
	tx_layout->setContentsMargins (0, 0, 0, 0);
	tx_window->setLayout (tx_layout);*/
    model->setHorizontalHeaderItem (0, new QStandardItem ("Type"));
    model->setHorizontalHeaderItem (1, new QStandardItem ("Account"));
    model->setHorizontalHeaderItem (2, new QStandardItem ("Amount"));
    model->setHorizontalHeaderItem (3, new QStandardItem ("Hash"));
    view->setModel (model);
	view->setEditTriggers (QAbstractItemView::NoEditTriggers);
	view->verticalHeader ()->hide ();
//	layout->addWidget (tx_window);
	layout->addWidget (view);
	layout->setContentsMargins (0, 0, 0, 0);
	window->setLayout (layout);
	tx_count->setValue (32);
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
		type = "Send";
		account = block_a.hashables.destination;
		amount = ledger.amount (transaction, block_a.hash ());
	}
	void receive_block (rai::receive_block const & block_a)
	{
		type = "Receive";
		account = ledger.account (transaction, block_a.source ());
		amount = ledger.amount (transaction, block_a.source ());
	}
	void open_block (rai::open_block const & block_a)
	{
		type = "Receive";
		if (block_a.hashables.source != rai::genesis_account)
		{
			account = ledger.account (transaction, block_a.hashables.source);
			amount = ledger.amount (transaction, block_a.hash ());
		}
		else
		{
			account = rai::genesis_account;
			amount = rai::genesis_amount;
		}
	}
	void change_block (rai::change_block const & block_a)
	{
		type = "Change";
		amount = 0;
		account = block_a.hashables.representative;
	}
	MDB_txn * transaction;
	rai::ledger & ledger;
	std::string type;
	rai::uint128_t amount;
	rai::account account;
};
}

void rai_qt::history::refresh ()
{
	rai::transaction transaction (ledger.store.environment, nullptr, false);
	model->removeRows (0, model->rowCount ());
	auto hash (ledger.latest (transaction, account));
	short_text_visitor visitor (transaction, ledger);
	for (auto i (0), n (tx_count->value ()); i < n && !hash.is_zero (); ++i)
	{
		QList <QStandardItem *> items;
		auto block (ledger.store.block_get (transaction, hash));
		assert (block != nullptr);
		block->visit (visitor);
		items.push_back (new QStandardItem (QString (visitor.type.c_str ())));
		items.push_back (new QStandardItem (QString (visitor.account.to_account ().c_str ())));
		items.push_back (new QStandardItem (QString (rai::amount (visitor.amount / rendering_ratio).to_string_dec ().c_str ())));
		items.push_back (new QStandardItem (QString (hash.to_string ().c_str ())));
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
rebroadcast (new QPushButton ("Rebroadcast")),
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
	layout->addWidget (rebroadcast);
	layout->addStretch ();
	layout->addWidget (back);
	window->setLayout (layout);
	QObject::connect (back, &QPushButton::released, [this] ()
	{
		this->wallet.pop_main_stack ();
	});
	QObject::connect (retrieve, &QPushButton::released, [this] ()
	{
		rai::block_hash hash_l;
		if (!hash_l.decode_hex (hash->text ().toStdString ()))
		{
			rai::transaction transaction (this->wallet.node.store.environment, nullptr, false);
			auto block_l (this->wallet.node.store.block_get (transaction, hash_l));
			if (block_l != nullptr)
			{
				std::string contents;
				block_l->serialize_json (contents);
				block->setPlainText (contents.c_str ());
				auto successor_l (this->wallet.node.store.block_successor (transaction, hash_l));
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
	QObject::connect (rebroadcast, &QPushButton::released, [this] ()
	{
		rai::block_hash block;
		auto error (block.decode_hex (hash->text ().toStdString ()));
		if (!error)
		{
			rai::transaction transaction (this->wallet.node.store.environment, nullptr, false);
			if (this->wallet.node.store.block_exists (transaction, block))
			{
				rebroadcast->setEnabled (false);
				this->wallet.node.background ([this, block] ()
				{
					rebroadcast_action (block);
				});
			}
		}
	});
}

void rai_qt::block_viewer::rebroadcast_action (rai::uint256_union const & hash_a)
{
	auto done (true);
	rai::transaction transaction (wallet.node.ledger.store.environment, nullptr, false);
	auto block (wallet.node.store.block_get (transaction, hash_a));
	if (block != nullptr)
	{
		wallet.node.network.republish_block (*block);
		auto successor (wallet.node.store.block_successor (transaction, hash_a));
		if (!successor.is_zero ())
		{
			done = false;
			wallet.node.alarm.add (std::chrono::system_clock::now () + std::chrono::seconds (1), [this, successor] ()
			{
				rebroadcast_action (successor);
			});
		}
	}
	if (done)
	{
		rebroadcast->setEnabled (true);
	}
}

rai_qt::account_viewer::account_viewer (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
account_label (new QLabel ("Account:")),
account_line (new QLineEdit),
refresh (new QPushButton ("Refresh")),
history (wallet_a.wallet_m->node.ledger, account, wallet_a.rendering_ratio),
back (new QPushButton ("Back")),
account (wallet_a.account),
wallet (wallet_a)
{
	layout->addWidget (account_label);
	layout->addWidget (account_line);
	layout->addWidget (refresh);
	layout->addWidget (history.window);
	layout->addWidget (back);
	window->setLayout (layout);
	QObject::connect (back, &QPushButton::released, [this] ()
	{
		this->wallet.pop_main_stack ();
	});
	QObject::connect (refresh, &QPushButton::released, [this] ()
	{
		account.clear ();
		if (!account.decode_account (account_line->text ().toStdString ()))
		{
			show_line_ok (*account_line);
			this->history.refresh ();
		}
		else
		{
			show_line_error (*account_line);
		}
	});
}

rai_qt::status::status (rai_qt::wallet & wallet_a) :
wallet (wallet_a)
{
	active.insert (rai_qt::status_types::nominal);
	set_text ();
}

void rai_qt::status::erase (rai_qt::status_types status_a)
{
	assert (status_a != rai_qt::status_types::nominal);
	auto erased (active.erase (status_a)); (void)erased;
	set_text ();
}

void rai_qt::status::insert (rai_qt::status_types status_a)
{
	assert (status_a != rai_qt::status_types::nominal);
	active.insert (status_a);
	set_text ();
}

void rai_qt::status::set_text ()
{
	wallet.status->setText (text ().c_str ());
	wallet.status->setStyleSheet ((std::string ("QLabel {") + color () + "}").c_str ());
}

std::string rai_qt::status::text ()
{
	assert (!active.empty ());
	std::string result;
    rai::transaction transaction (wallet.wallet_m->node.store.environment, nullptr, false);
    auto size (wallet.wallet_m->node.store.block_count (transaction));
    auto unchecked (wallet.wallet_m->node.store.unchecked_count (transaction));
    auto count_string (std::to_string (size.sum ()));

	switch (*active.begin ())
	{
		case rai_qt::status_types::disconnected:
			result = "Status: Disconnected";
			break;
		case rai_qt::status_types::working:
			result = "Status: Generating proof of work";
			break;
		case rai_qt::status_types::synchronizing:
			result = "Status: Synchronizing";
			break;
		case rai_qt::status_types::locked:
			result = "Status: Wallet locked";
			break;
		case rai_qt::status_types::vulnerable:
			result = "Status: Wallet password empty";
			break;
		case rai_qt::status_types::active:
			result = "Status: Wallet active";
			break;
		case rai_qt::status_types::nominal:
			result = "Status: Running";
			break;
		default:
			assert (false);
			break;
	}
    
    result += ", Block: ";
    if (unchecked != 0)
    {
        count_string += " (" + std::to_string (unchecked) + ")";
    }
    result += count_string.c_str ();
    
	return result;
}

std::string rai_qt::status::color ()
{
	assert (!active.empty ());
	std::string result;
	switch (*active.begin ())
	{
		case rai_qt::status_types::disconnected:
			result = "color: red";
			break;
		case rai_qt::status_types::working:
			result = "color: blue";
			break;
		case rai_qt::status_types::synchronizing:
			result = "color: red";
			break;
		case rai_qt::status_types::locked:
			result = "color: orange";
			break;
		case rai_qt::status_types::vulnerable:
			result = "color: blue";
			break;
		case rai_qt::status_types::active:
			result = "color: black";
			break;
		case rai_qt::status_types::nominal:
			result = "color: black";
			break;
		default:
			assert (false);
			break;
	}
	return result;
}

rai_qt::wallet::wallet (QApplication & application_a, rai::node & node_a, std::shared_ptr <rai::wallet> wallet_a, rai::account & account_a) :
rendering_ratio (rai::Mrai_ratio),
node (node_a),
wallet_m (wallet_a),
account (account_a),
history (node.ledger, account, rendering_ratio),
accounts (*this),
self (*this, account_a),
settings (*this),
advanced (*this),
block_creation (*this),
block_entry (*this),
block_viewer (*this),
account_viewer (*this),
import (*this),
application (application_a),
status (new QLabel),
main_stack (new QStackedWidget),
client_window (new QWidget),
client_layout (new QVBoxLayout),
entry_window (new QWidget),
entry_window_layout (new QVBoxLayout),
separator (new QFrame),
account_history_label (new QLabel ("Account history:")),
send_blocks (new QPushButton ("Send")),
settings_button (new QPushButton ("Settings")),
show_advanced (new QPushButton ("Advanced")),
send_blocks_window (new QWidget),
send_blocks_layout (new QVBoxLayout),
send_account_label (new QLabel ("Destination account:")),
send_account (new QLineEdit),
send_count_label (new QLabel ("Amount:")),
send_count (new QLineEdit),
send_blocks_send (new QPushButton ("Send")),
send_blocks_back (new QPushButton ("Back")),
active_status (*this)
{
	update_connected ();
    settings.update_locked (true, true);
    send_blocks_layout->addWidget (send_account_label);
	send_account->setPlaceholderText (rai::zero_key.pub.to_account ().c_str ());
    send_blocks_layout->addWidget (send_account);
    send_blocks_layout->addWidget (send_count_label);
	send_count->setPlaceholderText ("0");
    send_blocks_layout->addWidget (send_count);
    send_blocks_layout->addWidget (send_blocks_send);
    send_blocks_layout->addStretch ();
    send_blocks_layout->addWidget (send_blocks_back);
    send_blocks_layout->setContentsMargins (0, 0, 0, 0);
    send_blocks_window->setLayout (send_blocks_layout);
	
	entry_window_layout->addWidget (account_history_label);
	entry_window_layout->addWidget (history.window);
    entry_window_layout->addWidget (send_blocks);
	entry_window_layout->addWidget (settings_button);
    entry_window_layout->addWidget (show_advanced);
    entry_window_layout->setContentsMargins (0, 0, 0, 0);
    entry_window_layout->setSpacing (5);
    entry_window->setLayout (entry_window_layout);

    main_stack->addWidget (entry_window);

	status->setAlignment (Qt::AlignHCenter);
	separator->setFrameShape (QFrame::HLine);
	separator->setFrameShadow (QFrame::Sunken);
	
	client_layout->addWidget (status);
    client_layout->addWidget (self.window);
	client_layout->addWidget (separator);
    client_layout->addWidget (main_stack);
    client_layout->setSpacing (0);
    client_layout->setContentsMargins (0, 0, 0, 0);
    client_window->setLayout (client_layout);
    client_window->resize (320, 480);
	refresh ();
}

void rai_qt::wallet::start ()
{
	std::weak_ptr <rai_qt::wallet> this_w (shared_from_this ());
    QObject::connect (settings_button, &QPushButton::released, [this_w] ()
    {
		if (auto this_l = this_w.lock ())
		{
			this_l->settings.activate ();
		}
    });
    QObject::connect (show_advanced, &QPushButton::released, [this_w] ()
    {
		if (auto this_l = this_w.lock ())
		{
			this_l->push_main_stack (this_l->advanced.window);
		}
    });
    QObject::connect (send_blocks_send, &QPushButton::released, [this_w] ()
    {
		if (auto this_l = this_w.lock ())
		{
			show_line_ok (*this_l->send_count);
			show_line_ok (*this_l->send_account);
			rai::amount amount;
			if (!amount.decode_dec (this_l->send_count->text ().toStdString ()))
			{
				rai::uint128_t actual (amount.number () * this_l->rendering_ratio);
				if (actual / this_l->rendering_ratio == amount.number ())
				{
					QString account_text (this_l->send_account->text ());
					std::string account_text_narrow (account_text.toLocal8Bit ());
					rai::account account_l;
					auto parse_error (account_l.decode_account (account_text_narrow));
					if (!parse_error)
					{
						this_l->send_blocks_send->setEnabled (false);
						this_l->node.background ([this_w, account_l, actual] ()
						{
							if (auto this_l = this_w.lock ())
							{
								this_l->wallet_m->send_async (this_l->account, account_l, actual, [this_w] (std::unique_ptr <rai::block> block_a)
								{
									if (auto this_l = this_w.lock ())
									{
										auto succeeded (block_a != nullptr);
										this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w, succeeded] ()
										{
											if (auto this_l = this_w.lock ())
											{
												this_l->send_blocks_send->setEnabled (true);
												if (succeeded)
												{
													this_l->send_count->clear ();
													this_l->send_account->clear ();
													this_l->accounts.refresh ();
												}
												else
												{
													show_line_error (*this_l->send_count);
												}
											}
										}));
									}
								});
							}
						});
					}
					else
					{
						show_line_error (*this_l->send_account);
					}
				}
				else
				{
					show_line_error (*this_l->send_account);
				}
			}
			else
			{
				show_line_error (*this_l->send_count);
			}
		}
    });
    QObject::connect (send_blocks_back, &QPushButton::released, [this_w] ()
    {
		if (auto this_l = this_w.lock ())
		{
			this_l->pop_main_stack ();
		}
    });
    QObject::connect (send_blocks, &QPushButton::released, [this_w] ()
    {
		if (auto this_l = this_w.lock ())
		{
			this_l->push_main_stack (this_l->send_blocks_window);
		}
    });
    node.observers.blocks.add ([this_w] (rai::block const &, rai::account const & account_a, rai::amount const &)
    {
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w, account_a] ()
			{
				if (auto this_l = this_w.lock ())
				{
					if (this_l->wallet_m->exists (account_a))
					{
						this_l->accounts.refresh ();
					}
					if (account_a == this_l->account)
					{
						this_l->history.refresh ();
						this_l->self.refresh_balance ();
					}
				}
			}));
		}
    });
	node.observers.wallet.add ([this_w] (rai::account const & account_a, bool active_a)
	{
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w, account_a, active_a] ()
			{
				if (auto this_l = this_w.lock ())
				{
					if (this_l->account == account_a)
					{
						if (active_a)
						{
							this_l->active_status.insert (rai_qt::status_types::active);
						}
						else
						{
							this_l->active_status.erase (rai_qt::status_types::active);
						}
					}
				}
			}));
		}
	});
	node.observers.endpoint.add ([this_w] (rai::endpoint const &)
	{
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w] ()
			{
				if (auto this_l = this_w.lock ())
				{
					this_l->update_connected ();
				}
			}));
		}
	});
	node.observers.disconnect.add ([this_w] ()
	{
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w] ()
			{
				if (auto this_l = this_w.lock ())
				{
					this_l->update_connected ();
				}
			}));
		}
	});
	node.bootstrap_initiator.add_observer ([this_w] (bool active_a)
	{
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w, active_a] ()
			{
				if (auto this_l = this_w.lock ())
				{
					if (active_a)
					{
						this_l->active_status.insert (rai_qt::status_types::synchronizing);
					}
					else
					{
						this_l->active_status.erase (rai_qt::status_types::synchronizing);
					}
				}
			}));
		}
	});
	node.work.work_observers.add ([this_w] (bool working)
	{
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w, working] ()
			{
				if (auto this_l = this_w.lock ())
				{
					if (working)
					{
						this_l->active_status.insert (rai_qt::status_types::working);
					}
					else
					{
						this_l->active_status.erase (rai_qt::status_types::working);
					}
				}
			}));
		}
	});
	wallet_m->lock_observer = [this_w] (bool invalid, bool vulnerable)
	{
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w, invalid, vulnerable] ()
			{
				if (auto this_l = this_w.lock ())
				{
					this_l->settings.update_locked (invalid, vulnerable);
				}
			}));
		}
	};
}

void rai_qt::wallet::refresh ()
{
	{
		rai::transaction transaction (wallet_m->store.environment, nullptr, false);
		assert (wallet_m->store.exists (transaction, account));
	}
    self.account_text->setText (QString (account.to_account_split ().c_str ()));
	self.refresh_balance ();
    accounts.refresh ();
    history.refresh ();
	account_viewer.history.refresh ();
}

void rai_qt::wallet::update_connected ()
{
	if (node.peers.empty ())
	{
		active_status.insert (rai_qt::status_types::disconnected);
	}
	else
	{
		active_status.erase (rai_qt::status_types::disconnected);
	}
}

void rai_qt::wallet::change_rendering_ratio (rai::uint128_t const & rendering_ratio_a)
{
	application.postEvent (&processor, new eventloop_event ([this, rendering_ratio_a] ()
	{
		this->rendering_ratio = rendering_ratio_a;
		this->refresh ();
	}));
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

rai_qt::settings::settings (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
password (new QLineEdit),
lock_window (new QWidget),
lock_layout (new QHBoxLayout),
unlock (new QPushButton ("Unlock")),
lock (new QPushButton ("Lock")),
sep1 (new QFrame),
new_password (new QLineEdit),
retype_password (new QLineEdit),
change (new QPushButton ("Set/Change password")),
sep2 (new QFrame),
representative (new QLabel),
new_representative (new QLineEdit),
change_rep (new QPushButton ("Change representative")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
	password->setPlaceholderText("Password");
    password->setEchoMode (QLineEdit::EchoMode::Password);
    layout->addWidget (password);
	layout->addWidget (lock_window);
    lock_layout->addWidget (unlock);
    lock_layout->addWidget (lock);
	lock_window->setLayout (lock_layout);
	sep1->setFrameShape (QFrame::HLine);
	sep1->setFrameShadow (QFrame::Sunken);
	layout->addWidget (sep1);
    new_password->setEchoMode (QLineEdit::EchoMode::Password);
	new_password->setPlaceholderText ("New password");
    layout->addWidget (new_password);
    retype_password->setEchoMode (QLineEdit::EchoMode::Password);
	retype_password->setPlaceholderText ("Retype password");
    layout->addWidget (retype_password);
    layout->addWidget (change);
	sep2->setFrameShape (QFrame::HLine);
	sep2->setFrameShadow (QFrame::Sunken);
	layout->addWidget (sep2);
	layout->addWidget (representative);
	new_representative->setPlaceholderText (rai::zero_key.pub.to_account ().c_str ());
	layout->addWidget (new_representative);
	layout->addWidget (change_rep);
    layout->addStretch ();
    layout->addWidget (back);
    window->setLayout (layout);
    QObject::connect (change, &QPushButton::released, [this] ()
    {
		rai::transaction transaction (this->wallet.wallet_m->store.environment, nullptr, true);
        if (this->wallet.wallet_m->store.valid_password (transaction))
        {
            if (new_password->text () == retype_password->text ())
            {
				this->wallet.wallet_m->store.rekey (transaction, std::string (new_password->text ().toLocal8Bit ()));
                new_password->clear ();
				retype_password->clear ();
				retype_password->setPlaceholderText ("Retype password");
            }
            else
            {
				retype_password->clear ();
				retype_password->setPlaceholderText ("Password mismatch");
            }
        }
    });
	QObject::connect (change_rep, &QPushButton::released, [this] ()
	{
		rai::account representative_l;
		if (!representative_l.decode_account (new_representative->text ().toStdString ()))
		{
			change_rep->setEnabled (false);
			{
				rai::transaction transaction (this->wallet.wallet_m->store.environment, nullptr, true);
				this->wallet.wallet_m->store.representative_set (transaction, representative_l);
			}
			auto block (this->wallet.wallet_m->change_sync (this->wallet.account, representative_l));
			change_rep->setEnabled (true);
		}
	});
    QObject::connect (back, &QPushButton::released, [this] ()
    {
        assert (this->wallet.main_stack->currentWidget () == window);
		this->wallet.pop_main_stack ();
    });
    QObject::connect (unlock, &QPushButton::released, [this] ()
    {
		if (!this->wallet.wallet_m->enter_password (std::string (password->text ().toLocal8Bit ())))
		{
			password->clear ();
		}
    });
    QObject::connect (lock, &QPushButton::released, [this] ()
    {
        rai::raw_key empty;
        empty.data.clear ();
		this->wallet.wallet_m->store.password.value_set (empty);
        update_locked (true, true);
    });
}

void rai_qt::settings::activate ()
{
	this->wallet.push_main_stack (window);
}

void rai_qt::settings::update_locked (bool invalid, bool vulnerable)
{
	if (invalid)
	{
		this->wallet.active_status.insert (rai_qt::status_types::locked);
	}
	else
	{
		this->wallet.active_status.erase (rai_qt::status_types::locked);
	}
	if (vulnerable)
	{
		this->wallet.active_status.insert (rai_qt::status_types::vulnerable);
	}
	else
	{
		this->wallet.active_status.erase (rai_qt::status_types::vulnerable);
	}
}

rai_qt::advanced_actions::advanced_actions (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
block_count_text (new QLabel ("Block count:")),
block_count (new QLabel),
accounts (new QPushButton ("Accounts")),
show_ledger (new QPushButton ("Ledger")),
show_peers (new QPushButton ("Peers")),
search_for_receivables (new QPushButton ("Search for receivables")),
wallet_refresh (new QPushButton ("Refresh Wallet")),
create_block (new QPushButton ("Create Block")),
enter_block (new QPushButton ("Enter Block")),
block_viewer (new QPushButton ("Block Viewer")),
account_viewer (new QPushButton ("Account Viewer")),
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
bootstrap_label (new QLabel ("IPV6:port \"::ffff:192.168.0.1:7075\"")),
bootstrap_line (new QLineEdit),
peers_bootstrap (new QPushButton ("Initiate Bootstrap")),
peers_refresh (new QPushButton ("Refresh")),
peers_back (new QPushButton ("Back")),
wallet (wallet_a)
{
    ledger_model->setHorizontalHeaderItem (0, new QStandardItem ("Account"));
    ledger_model->setHorizontalHeaderItem (1, new QStandardItem ("Balance"));
    ledger_model->setHorizontalHeaderItem (2, new QStandardItem ("Block"));
    ledger_view->setModel (ledger_model);
    ledger_view->setEditTriggers (QAbstractItemView::NoEditTriggers);
    ledger_view->verticalHeader ()->hide ();
    ledger_layout->addWidget (ledger_view);
    ledger_layout->addWidget (ledger_refresh);
    ledger_layout->addWidget (ledger_back);
    ledger_layout->setContentsMargins (0, 0, 0, 0);
    ledger_window->setLayout (ledger_layout);
    
    peers_view->setEditTriggers (QAbstractItemView::NoEditTriggers);
    peers_view->setModel (peers_model);
    peers_layout->addWidget (peers_view);
	peers_layout->addWidget (bootstrap_label);
	peers_layout->addWidget (bootstrap_line);
	peers_layout->addWidget (peers_bootstrap);
    peers_layout->addWidget (peers_refresh);
    peers_layout->addWidget (peers_back);
    peers_layout->setContentsMargins (0, 0, 0, 0);
    peers_window->setLayout (peers_layout);

	layout->addWidget (block_count_text);
	layout->addWidget (block_count);
    layout->addWidget (accounts);
    layout->addWidget (show_ledger);
    layout->addWidget (show_peers);
    layout->addWidget (search_for_receivables);
    layout->addWidget (wallet_refresh);
    layout->addWidget (create_block);
    layout->addWidget (enter_block);
	layout->addWidget (block_viewer);
	layout->addWidget (account_viewer);
    layout->addStretch ();
    layout->addWidget (back);
    window->setLayout (layout);

    QObject::connect (accounts, &QPushButton::released, [this] ()
    {
		this->wallet.push_main_stack (wallet.accounts.window);
    });
    QObject::connect (wallet_refresh, &QPushButton::released, [this] ()
    {
		this->wallet.accounts.refresh ();
    });
    QObject::connect (show_peers, &QPushButton::released, [this] ()
    {
		this->wallet.push_main_stack (peers_window);
    });
    QObject::connect (show_ledger, &QPushButton::released, [this] ()
    {
		this->wallet.push_main_stack (ledger_window);
    });
    QObject::connect (back, &QPushButton::released, [this] ()
    {
		this->wallet.pop_main_stack ();
    });
    QObject::connect (peers_back, &QPushButton::released, [this] ()
    {
		this->wallet.pop_main_stack ();
    });
	QObject::connect (peers_bootstrap, &QPushButton::released, [this] ()
	{
		rai::endpoint endpoint;
		auto error (rai::parse_endpoint (bootstrap_line->text ().toStdString (), endpoint));
		if (!error)
		{
			show_line_ok (*bootstrap_line);
			bootstrap_line->clear ();
			this->wallet.node.bootstrap_initiator.bootstrap (endpoint);
		}
		else
		{
			show_line_error (*bootstrap_line);
		}
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
		this->wallet.pop_main_stack ();
    });
    QObject::connect (search_for_receivables, &QPushButton::released, [this] ()
    {
		this->wallet.wallet_m->search_pending ();
    });
    QObject::connect (create_block, &QPushButton::released, [this] ()
    {
		this->wallet.push_main_stack (this->wallet.block_creation.window);
    });
    QObject::connect (enter_block, &QPushButton::released, [this] ()
    {
		this->wallet.push_main_stack (this->wallet.block_entry.window);
    });
	QObject::connect (block_viewer, &QPushButton::released, [this] ()
	{
		this->wallet.push_main_stack (this->wallet.block_viewer.window);
	});
	QObject::connect (account_viewer, &QPushButton::released, [this] ()
	{
		this->wallet.push_main_stack (this->wallet.account_viewer.window);
	});
    refresh_ledger ();
	refresh_count ();
}

void rai_qt::advanced_actions::refresh_count ()
{
	rai::transaction transaction (wallet.wallet_m->node.store.environment, nullptr, false);
	auto size (wallet.wallet_m->node.store.block_count (transaction));
	auto unchecked (wallet.wallet_m->node.store.unchecked_count (transaction));
	auto count_string (std::to_string (size.sum ()));
	if (unchecked != 0)
	{
		count_string += " (" + std::to_string (unchecked) + ")";
	}
	block_count->setText (QString (count_string.c_str ()));
	wallet.node.alarm.add (std::chrono::system_clock::now () + std::chrono::seconds (5), [this] ()
	{
		this->wallet.application.postEvent (&this->wallet.processor, new eventloop_event ([this] ()
		{
			refresh_count ();
		}));
	});
}

void rai_qt::advanced_actions::refresh_peers ()
{
	auto list (wallet.node.peers.list ());
	std::sort (list.begin (), list.end (), [] (rai::peer_information const & lhs, rai::peer_information const & rhs)
	{
		return lhs.endpoint < rhs.endpoint;
	});
    QStringList peers;
    for (auto i: list)
    {
        std::stringstream endpoint;
        endpoint << i.address ().to_string ();
        endpoint << ':';
        endpoint << i.port ();
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
        items.push_back (new QStandardItem (QString (rai::block_hash (i->first).to_account ().c_str ())));
		auto hash (rai::account_info (i->second).head);
		std::string balance;
		rai::amount (wallet.node.ledger.balance (transaction, hash) / wallet.rendering_ratio).encode_dec (balance);
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
				show_label_ok (*status);
				this->status->setText ("");
				this->wallet.node.process_receive_republish (std::move (block_l));
            }
            else
            {
				show_label_error (*status);
				this->status->setText ("Unable to parse block");
            }
        }
        catch (std::runtime_error const &)
        {
			show_label_error (*status);
			this->status->setText ("Unable to parse block");
        }
    });
    QObject::connect (back, &QPushButton::released, [this] ()
    {
		this->wallet.pop_main_stack ();
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
		this->wallet.pop_main_stack ();
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
    auto error (account_l.decode_account (account->text ().toStdString ()));
    if (!error)
    {
        rai::amount amount_l;
        error = amount_l.decode_dec (amount->text ().toStdString ());
        if (!error)
        {
            rai::account destination_l;
            error = destination_l.decode_account (destination->text ().toStdString ());
            if (!error)
            {
				rai::transaction transaction (wallet.node.store.environment, nullptr, false);
                rai::raw_key key;
                if (!wallet.wallet_m->store.fetch (transaction, account_l, key))
                {
                    auto balance (wallet.node.ledger.account_balance (transaction, account_l));
                    if (amount_l.number () <= balance)
                    {
                        rai::account_info info;
                        auto error (wallet.node.store.account_get (transaction, account_l, info));
                        assert (!error);
                        rai::send_block send (info.head, destination_l, balance - amount_l.number (), key, account_l, wallet.wallet_m->work_fetch (transaction, account_l, info.head));
                        std::string block_l;
                        send.serialize_json (block_l);
                        block->setPlainText (QString (block_l.c_str ()));
						show_label_ok (*status);
                        status->setText ("Created block");
                    }
                    else
                    {
						show_label_error (*status);
                        status->setText ("Insufficient balance");
                    }
                }
                else
                {
					show_label_error (*status);
                    status->setText ("Account is not in wallet");
                }
            }
            else
            {
				show_label_error (*status);
                status->setText ("Unable to decode destination");
            }
        }
        else
        {
			show_label_error (*status);
            status->setText ("Unable to decode amount");
        }
    }
    else
    {
		show_label_error (*status);
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
		auto block_l (wallet.node.store.block_get (transaction, source_l));
		if (block_l != nullptr)
		{
			auto send_block (dynamic_cast <rai::send_block *> (block_l.get ()));
			if (send_block != nullptr)
			{
				rai::pending_key pending_key (send_block->hashables.destination, source_l);
				rai::pending_info pending;
				if (!wallet.node.store.pending_get (transaction, pending_key, pending))
				{
					rai::account_info info;
					auto error (wallet.node.store.account_get (transaction, pending_key.account, info));
					if (!error)
					{
						rai::raw_key key;
						auto error (wallet.wallet_m->store.fetch (transaction, pending_key.account, key));
						if (!error)
						{
							rai::receive_block receive (info.head, source_l, key, pending_key.account, wallet.wallet_m->work_fetch (transaction, pending_key.account, info.head));
							std::string block_l;
							receive.serialize_json (block_l);
							block->setPlainText (QString (block_l.c_str ()));
							show_label_ok (*status);
							status->setText ("Created block");
						}
						else
						{
							show_label_error (*status);
							status->setText ("Account is not in wallet");
						}
					}
					else
					{
						show_label_error (*status);
						status->setText ("Account not yet open");
					}
				}
				else
				{
					show_label_error (*status);
					status->setText ("Source block is not pending to receive");
				}
			}
			else
			{
				show_label_error (*status);
				status->setText("Source is not a send block");
			}
		}
		else
		{
			show_label_error (*status);
			status->setText("Source block not found");
		}
    }
    else
    {
		show_label_error (*status);
        status->setText ("Unable to decode source");
    }
}

void rai_qt::block_creation::create_change ()
{
    rai::account account_l;
    auto error (account_l.decode_account (account->text ().toStdString ()));
    if (!error)
    {
        rai::account representative_l;
        error = representative_l.decode_account (representative->text ().toStdString ());
        if (!error)
        {
			rai::transaction transaction (wallet.node.store.environment, nullptr, false);
            rai::account_info info;
            auto error (wallet.node.store.account_get (transaction, account_l, info));
            if (!error)
            {
                rai::raw_key key;
                auto error (wallet.wallet_m->store.fetch (transaction, account_l, key));
                if (!error)
                {
                    rai::change_block change (info.head, representative_l, key, account_l, wallet.wallet_m->work_fetch (transaction, account_l, info.head));
                    std::string block_l;
                    change.serialize_json (block_l);
                    block->setPlainText (QString (block_l.c_str ()));
					show_label_ok (*status);
                    status->setText ("Created block");
                }
                else
                {
					show_label_error (*status);
                    status->setText ("Account is not in wallet");
                }
            }
            else
            {
				show_label_error (*status);
                status->setText ("Account not yet open");
            }
        }
        else
        {
			show_label_error (*status);
            status->setText ("Unable to decode representative");
        }
    }
    else
    {
		show_label_error (*status);
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
        error = representative_l.decode_account (representative->text ().toStdString ());
        if (!error)
        {
			rai::transaction transaction (wallet.node.store.environment, nullptr, false);
			auto block_l (wallet.node.store.block_get (transaction, source_l));
			if (block_l != nullptr)
			{
				auto send_block (dynamic_cast <rai::send_block *> (block_l.get ()));
				if (send_block != nullptr)
				{
					rai::pending_key pending_key (send_block->hashables.destination, source_l);
					rai::pending_info pending;
					if (!wallet.node.store.pending_get (transaction, pending_key, pending))
					{
						rai::account_info info;
						auto error (wallet.node.store.account_get (transaction, pending_key.account, info));
						if (error)
						{
							rai::raw_key key;
							auto error (wallet.wallet_m->store.fetch (transaction, pending_key.account, key));
							if (!error)
							{
								rai::open_block open (source_l, representative_l, pending_key.account, key, pending_key.account, wallet.wallet_m->work_fetch (transaction, pending_key.account, pending_key.account));
								std::string block_l;
								open.serialize_json (block_l);
								block->setPlainText (QString (block_l.c_str ()));
								show_label_ok (*status);
								status->setText ("Created block");
							}
							else
							{
								show_label_error (*status);
								status->setText ("Account is not in wallet");
							}
						}
						else
						{
							show_label_error (*status);
							status->setText ("Account already open");
						}
					}
					else
					{
						show_label_error (*status);
						status->setText ("Source block is not pending to receive");
					}
				}
				else
				{
					show_label_error (*status);
					status->setText("Source is not a send block");
				}
			}
			else
			{
				show_label_error (*status);
				status->setText("Source block not found");
			}
        }
        else
        {
			show_label_error (*status);
            status->setText ("Unable to decode representative");
        }
    }
    else
    {
		show_label_error (*status);
        status->setText ("Unable to decode source");
    }
}
