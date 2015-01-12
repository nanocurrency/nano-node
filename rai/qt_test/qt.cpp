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
    std::string account_string;
    key.pub.encode_base58check (account_string);
    ASSERT_EQ (account_string, wallet.self.account_button->text ().toStdString ());
    ASSERT_EQ (1, wallet.accounts.model->rowCount ());
    auto item1 (wallet.accounts.model->item (0, 1));
    ASSERT_EQ (account_string, item1->text ().toStdString ());
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
    rai::keypair key1;
    std::string account;
    key1.pub.encode_base58check (account);
    system.wallet (1)->store.insert (key1.prv);
    int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::wallet wallet (application, *system.nodes [0], system.wallet (0), system.account (0));
    QTest::mouseClick (wallet.send_blocks, Qt::LeftButton);
    QTest::keyClicks (wallet.send_account, account.c_str ());
    QTest::keyClicks (wallet.send_count, "2");
    QTest::mouseClick (wallet.send_blocks_send, Qt::LeftButton);
    while (wallet.node.ledger.account_balance (key1.pub).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
    ASSERT_EQ (rai::scale_up (2), wallet.node.ledger.account_balance (key1.pub));
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
    rai::send_block send;
    send.hashables.destination = key1.pub;
    send.hashables.previous = system.nodes [0]->ledger.latest (rai::genesis_account);
    send.hashables.balance = 0;
    system.nodes [0]->work_create (send);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send.hash (), send.signature);
    std::string destination;
    send.hashables.destination.encode_base58check (destination);
    std::string previous;
    send.hashables.previous.encode_hex (previous);
    std::string balance;
    send.hashables.balance.encode_hex (balance);
    std::string signature;
    send.signature.encode_hex (signature);
    auto block_json (boost::str (boost::format ("{\"type\": \"send\", \"previous\": \"%1%\", \"balance\": \"%2%\", \"destination\": \"%3%\", \"work\": \"%4%\", \"signature\": \"%5%\"}") % previous % balance % destination % rai::to_string_hex (send.work) % signature));
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
	QTest::mouseClick (wallet.show_advanced, Qt::LeftButton);
	QTest::mouseClick (wallet.advanced.create_block, Qt::LeftButton);
	QTest::mouseClick (wallet.block_creation.send, Qt::LeftButton);
	std::string account;
	rai::test_genesis_key.pub.encode_base58check (account);
	QTest::keyClicks (wallet.block_creation.account, account.c_str ());
	QTest::keyClicks (wallet.block_creation.amount, "56bc75e2d63100000");
	std::string destination;
	key.pub.encode_base58check (destination);
	QTest::keyClicks (wallet.block_creation.destination, destination.c_str ());
	QTest::mouseClick (wallet.block_creation.create, Qt::LeftButton);
	std::string json (wallet.block_creation.block->toPlainText ().toStdString ());
	ASSERT_FALSE (json.empty ());
	rai::send_block send;
	boost::property_tree::ptree tree1;
	std::stringstream istream (json);
	boost::property_tree::read_json (istream, tree1);
	ASSERT_FALSE (send.deserialize_json (tree1));
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->ledger.process (send));
	ASSERT_EQ (rai::process_result::old, system.nodes [0]->ledger.process (send));
}

TEST (history, short_text)
{
	leveldb::Status init;
	rai::block_store store (init, rai::block_store_temp);
	ASSERT_TRUE (init.ok ());
	rai::genesis genesis;
	genesis.initialize (store);
	bool init1;
	rai::ledger ledger (init1, init, store);
	ASSERT_FALSE (init1);
	rai::keypair key;
	rai::send_block send;
	send.hashables.previous = ledger.latest (rai::test_genesis_key.pub);
	send.hashables.balance = 0;
	send.hashables.destination = rai::test_genesis_key.pub;
	rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send.hash (), send.signature);
	ASSERT_EQ (rai::process_result::progress, ledger.process (send));
	rai::receive_block receive;
	receive.hashables.previous = send.hash ();
	receive.hashables.source = send.hash ();
	rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, receive.hash (), receive.signature);
	ASSERT_EQ (rai::process_result::progress, ledger.process (receive));
	rai::change_block change (key.pub, receive.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub);
	ASSERT_EQ (rai::process_result::progress, ledger.process (change));
	int argc (0);
	QApplication application (argc, nullptr);
	rai_qt::history history (ledger, rai::test_genesis_key.pub);
	history.refresh ();
	ASSERT_EQ (4, history.model->rowCount ());
}