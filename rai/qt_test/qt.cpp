#include <gtest/gtest.h>

#include <rai/qt/qt.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <thread>
#include <QTest>

TEST (wallet, construction)
{
    rai::system system (24000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
	auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
    rai::keypair key;
    wallet_l->store.insert (key.prv);
    rai_qt::wallet wallet (application, *system.nodes [0], wallet_l, key.pub);
    ASSERT_EQ (key.pub.to_base58check (), wallet.self.account_button->text ().toStdString ());
    ASSERT_EQ (1, wallet.accounts.model->rowCount ());
    auto item1 (wallet.accounts.model->item (0, 1));
    ASSERT_EQ (key.pub.to_base58check (), item1->text ().toStdString ());
}

TEST (wallet, status)
{
    rai::system system (24000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
	auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
    rai::keypair key;
    wallet_l->store.insert (key.prv);
    rai_qt::wallet wallet (application, *system.nodes [0], wallet_l, key.pub);
	ASSERT_EQ ("Status: Disconnected", wallet.status->text ().toStdString ());
	system.nodes [0]->peers.insert (rai::endpoint (boost::asio::ip::address_v6::loopback (), 10000));
	ASSERT_EQ ("Status: Connected", wallet.status->text ().toStdString ());
	system.nodes [0]->peers.purge_list (std::chrono::system_clock::now () + std::chrono::seconds (5));
	ASSERT_EQ ("Status: Disconnected", wallet.status->text ().toStdString ());
}

TEST (wallet, startup_balance)
{
    rai::system system (24000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
	auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
    rai::keypair key;
    wallet_l->store.insert (key.prv);
    rai_qt::wallet wallet (application, *system.nodes [0], wallet_l, key.pub);
	ASSERT_EQ ("Balance: 0", wallet.self.balance_label->text().toStdString ());
}

TEST (wallet, select_account)
{
    rai::system system (24000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
	auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
    auto key1 (wallet_l->store.insert (1));
	auto key2 (wallet_l->store.insert (2));
    rai_qt::wallet wallet (application, *system.nodes [0], wallet_l, key1);
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
    int argc (0);
	QApplication application (argc, nullptr);
    auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
    rai::keypair key;
    wallet_l->store.insert (key.prv);
    rai_qt::wallet wallet (application, *system.nodes [0], wallet_l, key.pub);
    ASSERT_EQ (wallet.entry_window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.send_blocks, Qt::LeftButton);
    ASSERT_EQ (wallet.send_blocks_window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.send_blocks_back, Qt::LeftButton);
    ASSERT_EQ (wallet.entry_window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
    ASSERT_EQ (wallet.advanced.window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.advanced.change_password, Qt::LeftButton);
    ASSERT_EQ (wallet.password_change.window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.password_change.back, Qt::LeftButton);
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
    int argc (0);
	QApplication application (argc, nullptr);
    system.wallet (0)->store.insert (rai::keypair ().prv);
    rai_qt::wallet wallet (application, *system.nodes [0], system.wallet (0), system.account (0));
    QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
    QTest::mouseClick (wallet.advanced.change_password, Qt::LeftButton);
    ASSERT_NE (system.wallet (0)->store.derive_key ("1"), system.wallet (0)->store.password.value ());
    QTest::keyClicks (wallet.password_change.password, "1");
    QTest::keyClicks (wallet.password_change.retype, "1");
    QTest::mouseClick (wallet.password_change.change, Qt::LeftButton);
    ASSERT_EQ (system.wallet (0)->store.derive_key ("1"), system.wallet (0)->store.password.value ());
    ASSERT_EQ ("", wallet.password_change.password->text ());
    ASSERT_EQ ("", wallet.password_change.retype->text ());
}

TEST (client, password_nochange)
{
    rai::system system (24000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
    system.wallet (0)->store.insert (rai::keypair ().prv);
    rai_qt::wallet wallet (application, *system.nodes [0], system.wallet (0), system.account (0));
    QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
    QTest::mouseClick (wallet.advanced.change_password, Qt::LeftButton);
	auto iterations (0);
	while (system.wallet (0)->store.password.value () == 0)
	{
		system.service->poll_one ();
		system.processor.poll_one ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
    ASSERT_EQ (system.wallet (0)->store.derive_key (""), system.wallet (0)->store.password.value ());
    QTest::keyClicks (wallet.password_change.password, "1");
    QTest::keyClicks (wallet.password_change.retype, "2");
    QTest::mouseClick (wallet.password_change.change, Qt::LeftButton);
    ASSERT_EQ (system.wallet (0)->store.derive_key (""), system.wallet (0)->store.password.value ());
    ASSERT_EQ ("1", wallet.password_change.password->text ());
    ASSERT_EQ ("2", wallet.password_change.retype->text ());
}

TEST (wallet, enter_password)
{
    rai::system system (24000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
    system.wallet (0)->store.insert (rai::keypair ().prv);
    rai_qt::wallet wallet (application, *system.nodes [0], system.wallet (0), system.account (0));
    ASSERT_NE (-1, wallet.enter_password.layout->indexOf (wallet.enter_password.valid));
    ASSERT_NE (-1, wallet.enter_password.layout->indexOf (wallet.enter_password.password));
    ASSERT_NE (-1, wallet.enter_password.layout->indexOf (wallet.enter_password.unlock));
    ASSERT_NE (-1, wallet.enter_password.layout->indexOf (wallet.enter_password.lock));
    ASSERT_NE (-1, wallet.enter_password.layout->indexOf (wallet.enter_password.back));
    ASSERT_FALSE (system.wallet (0)->store.rekey ("abc"));
    QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
    QTest::mouseClick (wallet.advanced.enter_password, Qt::LeftButton);
    QTest::keyClicks (wallet.enter_password.password, "a");
    QTest::mouseClick (wallet.enter_password.unlock, Qt::LeftButton);
    ASSERT_EQ ("Password: INVALID", wallet.enter_password.valid->text ());
    wallet.enter_password.password->setText ("");
    QTest::keyClicks (wallet.enter_password.password, "abc");
    QTest::mouseClick (wallet.enter_password.unlock, Qt::LeftButton);
    ASSERT_EQ ("Password: Valid", wallet.enter_password.valid->text ());
    ASSERT_EQ ("", wallet.enter_password.password->text ());
}

TEST (wallet, send)
{
    rai::system system (24000, 2);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    auto key1 (system.wallet (1)->store.insert (1));
    int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::wallet wallet (application, *system.nodes [0], system.wallet (0), rai::test_genesis_key.pub);
    QTest::mouseClick (wallet.send_blocks, Qt::LeftButton);
    QTest::keyClicks (wallet.send_account, key1.to_base58check ().c_str ());
    QTest::keyClicks (wallet.send_count, "2");
    QTest::mouseClick (wallet.send_blocks_send, Qt::LeftButton);
	auto iterations1 (0);
    while (wallet.node.ledger.account_balance (key1).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
		++iterations1;
		ASSERT_LT (iterations1, 200);
    }
    ASSERT_EQ (rai::scale_up (2), wallet.node.ledger.account_balance (key1));
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
    int argc (0);
    QApplication application (argc, nullptr);
    system.wallet (0)->store.insert (rai::keypair ().prv);
    rai_qt::wallet wallet (application, *system.nodes [0], system.wallet (0), system.account (0));
    ASSERT_EQ ("Process", wallet.block_entry.process->text ());
    ASSERT_EQ ("Back", wallet.block_entry.back->text ());
    rai::keypair key1;
    ASSERT_EQ (wallet.entry_window, wallet.main_stack->currentWidget ());
    QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
    QTest::mouseClick (wallet.advanced.enter_block, Qt::LeftButton);
    ASSERT_EQ (wallet.block_entry.window, wallet.main_stack->currentWidget ());
    rai::send_block send (key1.pub, system.nodes [0]->ledger.latest (rai::genesis_account), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (system.nodes [0]->ledger.latest (rai::genesis_account)));
    std::string previous;
    send.hashables.previous.encode_hex (previous);
    std::string balance;
    send.hashables.balance.encode_hex (balance);
    std::string signature;
    send.signature.encode_hex (signature);
    auto block_json (boost::str (boost::format ("{\"type\": \"send\", \"previous\": \"%1%\", \"balance\": \"%2%\", \"destination\": \"%3%\", \"work\": \"%4%\", \"signature\": \"%5%\"}") % previous % balance % send.hashables.destination.to_base58check () % rai::to_string_hex (send.work) % signature));
    QTest::keyClicks (wallet.block_entry.block, block_json.c_str ());
    QTest::mouseClick (wallet.block_entry.process, Qt::LeftButton);
    ASSERT_EQ (send.hash (), system.nodes [0]->ledger.latest (rai::genesis_account));
    QTest::mouseClick(wallet.block_entry.back, Qt::LeftButton);
    ASSERT_EQ (wallet.advanced.window, wallet.main_stack->currentWidget ());
}

TEST (wallet, create_send)
{
	rai::keypair key;
	rai::system system (24000, 1);
	system.wallet (0)->store.insert (rai::test_genesis_key.prv);
	system.wallet (0)->store.insert (key.prv);
	int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::wallet wallet (application, *system.nodes [0], system.wallet (0), rai::test_genesis_key.pub);
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
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->ledger.process (send));
	ASSERT_EQ (rai::process_result::old, system.nodes [0]->ledger.process (send));
}

TEST (wallet, create_open_receive)
{
	rai::keypair key;
	rai::system system (24000, 1);
	system.wallet (0)->store.insert (rai::test_genesis_key.prv);
	system.wallet (0)->send_all (key.pub, 100);
	auto latest1 (system.nodes [0]->ledger.latest (rai::test_genesis_key.pub));
	system.wallet (0)->send_all (key.pub, 100);
	auto latest2 (system.nodes [0]->ledger.latest (rai::test_genesis_key.pub));
	ASSERT_NE (latest1, latest2);
	system.wallet (0)->store.insert (key.prv);
	int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::wallet wallet (application, *system.nodes [0], system.wallet (0), rai::test_genesis_key.pub);
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
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->ledger.process (open));
	ASSERT_EQ (rai::process_result::old, system.nodes [0]->ledger.process (open));
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
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->ledger.process (receive));
	ASSERT_EQ (rai::process_result::old, system.nodes [0]->ledger.process (receive));
}

TEST (wallet, create_change)
{
	rai::keypair key;
	rai::system system (24000, 1);
	system.wallet (0)->store.insert (rai::test_genesis_key.prv);
	int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::wallet wallet (application, *system.nodes [0], system.wallet (0), rai::test_genesis_key.pub);
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
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->ledger.process (change));
	ASSERT_EQ (rai::process_result::old, system.nodes [0]->ledger.process (change));
}

TEST (history, short_text)
{
	bool init;
	rai::block_store store (init, rai::unique_path ());
	ASSERT_TRUE (!init);
	rai::genesis genesis;
	genesis.initialize (store);
	rai::ledger ledger (store);
	rai::keypair key;
	rai::send_block send (rai::test_genesis_key.pub, ledger.latest (rai::test_genesis_key.pub), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (ledger.latest (rai::test_genesis_key.pub)));
	ASSERT_EQ (rai::process_result::progress, ledger.process (send));
	rai::receive_block receive (send.hash (), send.hash (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, ledger.process (receive));
	rai::change_block change (key.pub, receive.hash (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, ledger.process (change));
	int argc (0);
	QApplication application (argc, nullptr);
	rai_qt::history history (ledger, rai::test_genesis_key.pub);
	history.refresh ();
	ASSERT_EQ (4, history.model->rowCount ());
}

TEST (wallet, startup_work)
{
	rai::keypair key;
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (key.prv);
    int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::wallet wallet (application, *system.nodes [0], system.wallet (0), system.account (0));
    QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
	uint64_t work1;
    ASSERT_TRUE (wallet.wallet_m->store.work_get (rai::test_genesis_key.pub, work1));
	QTest::mouseClick (wallet.advanced.accounts, Qt::LeftButton);
	QTest::keyClicks (wallet.accounts.account_key_line, "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4");
    QTest::mouseClick (wallet.accounts.account_key_button, Qt::LeftButton);
    auto iterations1 (0);
    while (wallet.wallet_m->store.work_get (rai::test_genesis_key.pub, work1))
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
    }
}

TEST (wallet, block_viewer)
{
	rai::keypair key;
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (key.prv);
    int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::wallet wallet (application, *system.nodes [0], system.wallet (0), system.account (0));
    QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
	ASSERT_NE (-1, wallet.advanced.layout->indexOf (wallet.advanced.block_viewer));
	QTest::mouseClick (wallet.advanced.block_viewer, Qt::LeftButton);
	ASSERT_EQ (wallet.block_viewer.window, wallet.main_stack->currentWidget ());
	QTest::keyClicks (wallet.block_viewer.hash, system.nodes [0]->ledger.latest (rai::genesis_account).to_string ().c_str ());
	QTest::mouseClick (wallet.block_viewer.retrieve, Qt::LeftButton);
	ASSERT_FALSE (wallet.block_viewer.block->toPlainText ().toStdString ().empty ());
	QTest::mouseClick (wallet.block_viewer.back, Qt::LeftButton);
	ASSERT_EQ (wallet.advanced.window, wallet.main_stack->currentWidget ());
}