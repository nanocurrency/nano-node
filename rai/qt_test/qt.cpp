#include <gtest/gtest.h>

#include <rai/qt/qt.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <thread>
#include <QTest>

extern QApplication * test_application;

TEST (wallet, construction)
{
    rai::system system (24000, 1);
	auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
    rai::keypair key;
	wallet_l->insert (key.prv);
    rai_qt::wallet wallet (*test_application, *system.nodes [0], wallet_l, key.pub);
    ASSERT_EQ (key.pub.to_base58check (), wallet.self.account_button->text ().toStdString ());
    ASSERT_EQ (1, wallet.accounts.model->rowCount ());
    auto item1 (wallet.accounts.model->item (0, 1));
    ASSERT_EQ (key.pub.to_base58check (), item1->text ().toStdString ());
}

TEST (wallet, status)
{
    rai::system system (24000, 1);
	auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
    rai::keypair key;
	wallet_l->insert (key.prv);
	rai_qt::wallet wallet (*test_application, *system.nodes [0], wallet_l, key.pub);
	ASSERT_EQ ("Status: Disconnected", wallet.status->text ().toStdString ());
	system.nodes [0]->peers.insert (rai::endpoint (boost::asio::ip::address_v6::loopback (), 10000));
	ASSERT_EQ ("Status: Connected", wallet.status->text ().toStdString ());
	system.nodes [0]->peers.purge_list (std::chrono::system_clock::now () + std::chrono::seconds (5));
	ASSERT_EQ ("Status: Disconnected", wallet.status->text ().toStdString ());
}

TEST (wallet, startup_balance)
{
    rai::system system (24000, 1);
	auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
    rai::keypair key;
	wallet_l->insert (key.prv);
    rai_qt::wallet wallet (*test_application, *system.nodes [0], wallet_l, key.pub);
	ASSERT_EQ ("Balance: 0", wallet.self.balance_label->text().toStdString ());
}

TEST (wallet, select_account)
{
    rai::system system (24000, 1);
	auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
	rai::public_key key1 (wallet_l->insert (1));
	rai::public_key key2 (wallet_l->insert (2));
    rai_qt::wallet wallet (*test_application, *system.nodes [0], wallet_l, key1);
	ASSERT_EQ (key1, wallet.account);
	QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
	QTest::mouseClick (wallet.advanced.accounts, Qt::LeftButton);
	wallet.accounts.view->selectionModel ()->setCurrentIndex (wallet.accounts.model->index (0, 1), QItemSelectionModel::SelectionFlag::Select);
	QTest::mouseClick (wallet.accounts.use_account, Qt::LeftButton);
	ASSERT_EQ (key2, wallet.account);
}

TEST (wallet, main)
{
    rai::system system (24000, 1);
    auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
    rai::keypair key;
	wallet_l->insert (key.prv);
    rai_qt::wallet wallet (*test_application, *system.nodes [0], wallet_l, key.pub);
    ASSERT_EQ (wallet.entry_window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.send_blocks, Qt::LeftButton);
    ASSERT_EQ (wallet.send_blocks_window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.send_blocks_back, Qt::LeftButton);
    QTest::mouseClick (wallet.settings_button, Qt::LeftButton);
    ASSERT_EQ (wallet.settings.window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.settings.back, Qt::LeftButton);
    ASSERT_EQ (wallet.entry_window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
    ASSERT_EQ (wallet.advanced.window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.advanced.show_ledger, Qt::LeftButton);
    ASSERT_EQ (wallet.advanced.ledger_window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.advanced.ledger_back, Qt::LeftButton);
    ASSERT_EQ (wallet.advanced.window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.advanced.show_peers, Qt::LeftButton);
    ASSERT_EQ (wallet.advanced.peers_window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.advanced.peers_back, Qt::LeftButton);
    ASSERT_EQ (wallet.advanced.window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.advanced.back, Qt::LeftButton);
    ASSERT_EQ (wallet.entry_window, wallet.main_stack->currentWidget ());
}

TEST (wallet, password_change)
{
    rai::system system (24000, 1);
	rai::account account;
	system.wallet (0)->insert (rai::keypair ().prv);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		account = system.account (transaction, 0);
	}
    rai_qt::wallet wallet (*test_application, *system.nodes [0], system.wallet (0), account);
    QTest::mouseClick (wallet.settings_button, Qt::LeftButton);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		ASSERT_NE (system.wallet (0)->store.derive_key (transaction, "1"), system.wallet (0)->store.password.value ());
	}
    QTest::keyClicks (wallet.settings.new_password, "1");
    QTest::keyClicks (wallet.settings.retype_password, "1");
    QTest::mouseClick (wallet.settings.change, Qt::LeftButton);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		ASSERT_EQ (system.wallet (0)->store.derive_key (transaction, "1"), system.wallet (0)->store.password.value ());
	}
    ASSERT_EQ ("", wallet.settings.new_password->text ());
    ASSERT_EQ ("", wallet.settings.retype_password->text ());
}

TEST (client, password_nochange)
{
    rai::system system (24000, 1);
	rai::account account;
	system.wallet (0)->insert (rai::keypair ().prv);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		account = system.account (transaction, 0);
	}
    rai_qt::wallet wallet (*test_application, *system.nodes [0], system.wallet (0), account);
    QTest::mouseClick (wallet.settings_button, Qt::LeftButton);
	auto iterations (0);
	while (system.wallet (0)->store.password.value () == 0)
	{
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		ASSERT_EQ (system.wallet (0)->store.derive_key (transaction, ""), system.wallet (0)->store.password.value ());
	}
    QTest::keyClicks (wallet.settings.new_password, "1");
    QTest::keyClicks (wallet.settings.retype_password, "2");
    QTest::mouseClick (wallet.settings.change, Qt::LeftButton);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		ASSERT_EQ (system.wallet (0)->store.derive_key (transaction, ""), system.wallet (0)->store.password.value ());
	}
    ASSERT_EQ ("1", wallet.settings.new_password->text ());
    ASSERT_EQ ("", wallet.settings.retype_password->text ());
}

TEST (wallet, enter_password)
{
    rai::system system (24000, 1);
	rai::account account;
	system.wallet (0)->insert (rai::keypair ().prv);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		account = system.account (transaction, 0);
	}
    rai_qt::wallet wallet (*test_application, *system.nodes [0], system.wallet (0), account);
    ASSERT_NE (-1, wallet.settings.layout->indexOf (wallet.settings.valid));
    ASSERT_NE (-1, wallet.settings.layout->indexOf (wallet.settings.password));
    ASSERT_NE (-1, wallet.settings.lock_layout->indexOf (wallet.settings.unlock));
    ASSERT_NE (-1, wallet.settings.lock_layout->indexOf (wallet.settings.lock));
    ASSERT_NE (-1, wallet.settings.layout->indexOf (wallet.settings.back));
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		ASSERT_FALSE (system.wallet (0)->store.rekey (transaction, "abc"));
	}
    QTest::mouseClick (wallet.settings_button, Qt::LeftButton);
    QTest::keyClicks (wallet.settings.new_password, "a");
    QTest::mouseClick (wallet.settings.unlock, Qt::LeftButton);
    ASSERT_EQ ("Wallet: LOCKED", wallet.settings.valid->text ());
    wallet.settings.new_password->setText ("");
    QTest::keyClicks (wallet.settings.password, "abc");
    QTest::mouseClick (wallet.settings.unlock, Qt::LeftButton);
    ASSERT_EQ ("Wallet: Unlocked", wallet.settings.valid->text ());
    ASSERT_EQ ("", wallet.settings.password->text ());
}

TEST (wallet, send)
{
    rai::system system (24000, 2);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	rai::public_key key1 (system.wallet (1)->insert (1));
    rai_qt::wallet wallet (*test_application, *system.nodes [0], system.wallet (0), rai::test_genesis_key.pub);
    QTest::mouseClick (wallet.send_blocks, Qt::LeftButton);
    QTest::keyClicks (wallet.send_account, key1.to_base58check ().c_str ());
    QTest::keyClicks (wallet.send_count, "2");
    QTest::mouseClick (wallet.send_blocks_send, Qt::LeftButton);
	auto iterations1 (0);
    while (wallet.node.balance (key1).is_zero ())
    {
        system.poll ();
		++iterations1;
		ASSERT_LT (iterations1, 200);
    }
	rai::uint128_t amount (wallet.node.balance (key1));
    ASSERT_EQ (2 * wallet.rendering_ratio, amount);
	QTest::mouseClick (wallet.send_blocks_back, Qt::LeftButton);
    QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
	QTest::mouseClick (wallet.advanced.show_ledger, Qt::LeftButton);
	QTest::mouseClick (wallet.advanced.ledger_refresh, Qt::LeftButton);
	ASSERT_EQ (2, wallet.advanced.ledger_model->rowCount ());
	ASSERT_EQ (3, wallet.advanced.ledger_model->columnCount ());
	auto item (wallet.advanced.ledger_model->itemFromIndex (wallet.advanced.ledger_model->index (1, 1)));
	ASSERT_EQ ("2", item->text ().toStdString ());
}


TEST (wallet, process_block)
{
    rai::system system (24000, 1);
	rai::account account;
	rai::block_hash latest (system.nodes [0]->latest (rai::genesis_account));
	system.wallet (0)->insert (rai::keypair ().prv);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		account = system.account (transaction, 0);
	}
    rai_qt::wallet wallet (*test_application, *system.nodes [0], system.wallet (0), account);
    ASSERT_EQ ("Process", wallet.block_entry.process->text ());
    ASSERT_EQ ("Back", wallet.block_entry.back->text ());
    rai::keypair key1;
    ASSERT_EQ (wallet.entry_window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
    QTest::mouseClick (wallet.advanced.enter_block, Qt::LeftButton);
    ASSERT_EQ (wallet.block_entry.window, wallet.main_stack->currentWidget ());
    rai::send_block send (latest, key1.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (latest));
    std::string previous;
    send.hashables.previous.encode_hex (previous);
    std::string balance;
    send.hashables.balance.encode_hex (balance);
    std::string signature;
    send.signature.encode_hex (signature);
    auto block_json (boost::str (boost::format ("{\"type\": \"send\", \"previous\": \"%1%\", \"balance\": \"%2%\", \"destination\": \"%3%\", \"work\": \"%4%\", \"signature\": \"%5%\"}") % previous % balance % send.hashables.destination.to_base58check () % rai::to_string_hex (send.work) % signature));
    QTest::keyClicks (wallet.block_entry.block, block_json.c_str ());
    QTest::mouseClick (wallet.block_entry.process, Qt::LeftButton);
    ASSERT_EQ (send.hash (), system.nodes [0]->latest (rai::genesis_account));
    QTest::mouseClick(wallet.block_entry.back, Qt::LeftButton);
    ASSERT_EQ (wallet.advanced.window, wallet.main_stack->currentWidget ());
}

TEST (wallet, create_send)
{
	rai::keypair key;
	rai::system system (24000, 1);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	system.wallet (0)->insert (key.prv);
    rai_qt::wallet wallet (*test_application, *system.nodes [0], system.wallet (0), rai::test_genesis_key.pub);
	wallet.client_window->show ();
	QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
	QTest::mouseClick (wallet.advanced.create_block, Qt::LeftButton);
	QTest::mouseClick (wallet.block_creation.send, Qt::LeftButton);
	QTest::keyClicks (wallet.block_creation.account, rai::test_genesis_key.pub.to_base58check ().c_str ());
	QTest::keyClicks (wallet.block_creation.amount, "56bc75e2d63100000");
	QTest::keyClicks (wallet.block_creation.destination, key.pub.to_base58check ().c_str ());
	QTest::mouseClick (wallet.block_creation.create, Qt::LeftButton);
	std::string json (wallet.block_creation.block->toPlainText ().toStdString ());
	ASSERT_FALSE (json.empty ());
	boost::property_tree::ptree tree1;
	std::stringstream istream (json);
	boost::property_tree::read_json (istream, tree1);
	bool error;
	rai::send_block send (error, tree1);
	ASSERT_FALSE (error);
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (send).code);
	ASSERT_EQ (rai::process_result::old, system.nodes [0]->process (send).code);
}

TEST (wallet, create_open_receive)
{
	rai::keypair key;
	rai::system system (24000, 1);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	system.wallet (0)->send_sync (rai::test_genesis_key.pub, key.pub, 100);
	rai::block_hash latest1 (system.nodes [0]->latest (rai::test_genesis_key.pub));
	system.wallet (0)->send_sync (rai::test_genesis_key.pub, key.pub, 100);
	rai::block_hash latest2 (system.nodes [0]->latest (rai::test_genesis_key.pub));
	ASSERT_NE (latest1, latest2);
	system.wallet (0)->insert (key.prv);
    rai_qt::wallet wallet (*test_application, *system.nodes [0], system.wallet (0), rai::test_genesis_key.pub);
	wallet.client_window->show ();
	QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
	QTest::mouseClick (wallet.advanced.create_block, Qt::LeftButton);
	QTest::mouseClick (wallet.block_creation.open, Qt::LeftButton);
	QTest::keyClicks (wallet.block_creation.source, latest1.to_string ().c_str ());
	QTest::keyClicks (wallet.block_creation.representative, rai::test_genesis_key.pub.to_base58check ().c_str ());
	QTest::mouseClick (wallet.block_creation.create, Qt::LeftButton);
	std::string json1 (wallet.block_creation.block->toPlainText ().toStdString ());
	ASSERT_FALSE (json1.empty ());
	boost::property_tree::ptree tree1;
	std::stringstream istream1 (json1);
	boost::property_tree::read_json (istream1, tree1);
	bool error;
	rai::open_block open (error, tree1);
	ASSERT_FALSE (error);
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (open).code);
	ASSERT_EQ (rai::process_result::old, system.nodes [0]->process (open).code);
	wallet.block_creation.block->clear ();
	wallet.block_creation.source->clear ();
	QTest::mouseClick (wallet.block_creation.receive, Qt::LeftButton);
	QTest::keyClicks (wallet.block_creation.source, latest2.to_string ().c_str ());
	QTest::mouseClick (wallet.block_creation.create, Qt::LeftButton);
	std::string json2 (wallet.block_creation.block->toPlainText ().toStdString ());
	ASSERT_FALSE (json2.empty ());
	boost::property_tree::ptree tree2;
	std::stringstream istream2 (json2);
	boost::property_tree::read_json (istream2, tree2);
	bool error2;
	rai::receive_block receive (error2, tree2);
	ASSERT_FALSE (error2);
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (receive).code);
	ASSERT_EQ (rai::process_result::old, system.nodes [0]->process (receive).code);
}

TEST (wallet, create_change)
{
	rai::keypair key;
	rai::system system (24000, 1);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    rai_qt::wallet wallet (*test_application, *system.nodes [0], system.wallet (0), rai::test_genesis_key.pub);
	wallet.client_window->show ();
	QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
	QTest::mouseClick (wallet.advanced.create_block, Qt::LeftButton);
	QTest::mouseClick (wallet.block_creation.change, Qt::LeftButton);
	QTest::keyClicks (wallet.block_creation.account, rai::test_genesis_key.pub.to_base58check ().c_str ());
	QTest::keyClicks (wallet.block_creation.representative, key.pub.to_base58check ().c_str ());
	QTest::mouseClick (wallet.block_creation.create, Qt::LeftButton);
	std::string json (wallet.block_creation.block->toPlainText ().toStdString ());
	ASSERT_FALSE (json.empty ());
	boost::property_tree::ptree tree1;
	std::stringstream istream (json);
	boost::property_tree::read_json (istream, tree1);
	bool error (false);
	rai::change_block change (error, tree1);
	ASSERT_FALSE (error);
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (change).code);
	ASSERT_EQ (rai::process_result::old, system.nodes [0]->process (change).code);
}

TEST (history, short_text)
{
	bool init;
	rai::block_store store (init, rai::unique_path ());
	ASSERT_TRUE (!init);
	rai::genesis genesis;
	rai::ledger ledger (store);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		genesis.initialize (transaction, store);
		rai::keypair key;
		rai::send_block send (ledger.latest (transaction, rai::test_genesis_key.pub), rai::test_genesis_key.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send).code);
		rai::receive_block receive (send.hash (), send.hash (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, receive).code);
		rai::change_block change (receive.hash (), key.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, change).code);
	}
	rai_qt::history history (ledger, rai::test_genesis_key.pub, rai::Grai_ratio);
	history.refresh ();
	ASSERT_EQ (4, history.model->rowCount ());
}

TEST (wallet, startup_work)
{
	rai::keypair key;
    rai::system system (24000, 1);
	system.wallet (0)->insert (key.prv);
	rai::account account;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		account = system.account (transaction, 0);
	}
    rai_qt::wallet wallet (*test_application, *system.nodes [0], system.wallet (0), account);
    QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
	uint64_t work1;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		ASSERT_TRUE (wallet.wallet_m->store.work_get (transaction, rai::test_genesis_key.pub, work1));
	}
	QTest::mouseClick (wallet.advanced.accounts, Qt::LeftButton);
	QTest::keyClicks (wallet.accounts.account_key_line, "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4");
    QTest::mouseClick (wallet.accounts.account_key_button, Qt::LeftButton);
    auto iterations1 (0);
	auto again (true);
    while (again)
    {
        system.poll ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again = wallet.wallet_m->store.work_get (transaction, rai::test_genesis_key.pub, work1);
    }
}

TEST (wallet, block_viewer)
{
	rai::keypair key;
    rai::system system (24000, 1);
	system.wallet (0)->insert (key.prv);
	rai::account account;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		account = system.account (transaction, 0);
	}
    rai_qt::wallet wallet (*test_application, *system.nodes [0], system.wallet (0), account);
    QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
	ASSERT_NE (-1, wallet.advanced.layout->indexOf (wallet.advanced.block_viewer));
	QTest::mouseClick (wallet.advanced.block_viewer, Qt::LeftButton);
	ASSERT_EQ (wallet.block_viewer.window, wallet.main_stack->currentWidget ());
	rai::block_hash latest (system.nodes [0]->latest (rai::genesis_account));
	QTest::keyClicks (wallet.block_viewer.hash, latest.to_string ().c_str ());
	QTest::mouseClick (wallet.block_viewer.retrieve, Qt::LeftButton);
	ASSERT_FALSE (wallet.block_viewer.block->toPlainText ().toStdString ().empty ());
	QTest::mouseClick (wallet.block_viewer.back, Qt::LeftButton);
	ASSERT_EQ (wallet.advanced.window, wallet.main_stack->currentWidget ());
}

TEST (wallet, import)
{
    rai::system system (24000, 2);
	std::string json;
	rai::keypair key1;
	rai::keypair key2;
	system.wallet (0)->insert (key1.prv);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		system.wallet (0)->store.serialize_json (transaction, json);
	}
	system.wallet (1)->insert (key2.prv);
	auto path (rai::unique_path ());
	{
		std::ofstream stream;
		stream.open (path.string ().c_str ());
		stream << json;
	}
    rai_qt::wallet wallet (*test_application, *system.nodes [1], system.wallet (1), key2.pub);
	QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
	ASSERT_EQ (wallet.advanced.window, wallet.main_stack->currentWidget ());
	QTest::mouseClick (wallet.advanced.accounts, Qt::LeftButton);
	ASSERT_EQ (wallet.accounts.window, wallet.main_stack->currentWidget ());
	QTest::mouseClick (wallet.accounts.import_wallet, Qt::LeftButton);
	ASSERT_EQ (wallet.import.window, wallet.main_stack->currentWidget ());
	QTest::keyClicks (wallet.import.filename, path.string ().c_str ());
	QTest::keyClicks (wallet.import.password, "");
	ASSERT_FALSE (system.wallet (1)->exists (key1.pub));
	QTest::mouseClick (wallet.import.perform, Qt::LeftButton);
	ASSERT_TRUE (system.wallet (1)->exists (key1.pub));
}

TEST (wallet, republish)
{
    rai::system system (24000, 2);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	rai::keypair key;
	rai::block_hash hash;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		rai::send_block block (system.nodes [0]->ledger.latest (transaction, rai::test_genesis_key.pub), key.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		hash = block.hash ();
		ASSERT_EQ (rai::process_result::progress, system.nodes [0]->ledger.process (transaction, block).code);
	}
    rai_qt::wallet wallet (*test_application, *system.nodes [0], system.wallet (0), rai::test_genesis_key.pub);
	QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
	ASSERT_EQ (wallet.advanced.window, wallet.main_stack->currentWidget ());
	QTest::mouseClick (wallet.advanced.block_viewer, Qt::LeftButton);
	ASSERT_EQ (wallet.block_viewer.window, wallet.main_stack->currentWidget ());
	QTest::keyClicks (wallet.block_viewer.hash, hash.to_string ().c_str ());
	QTest::mouseClick (wallet.block_viewer.rebroadcast, Qt::LeftButton);
	ASSERT_FALSE (system.nodes [1]->balance (rai::test_genesis_key.pub).is_zero ());
	int iterations (0);
	while (system.nodes [1]->balance (rai::test_genesis_key.pub).is_zero ())
	{
		++iterations;
		ASSERT_LT (iterations, 200);
		system.poll ();
	}
}