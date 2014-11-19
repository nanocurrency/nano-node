#include <gtest/gtest.h>

#include <rai/qt/qt.hpp>
#include <thread>
#include <QTest>

TEST (client, construction)
{
    rai::system system (24000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::client client (application, *system.clients [0]);
}

TEST (client, main)
{
    rai::system system (24000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::client client (application, *system.clients [0]);
    ASSERT_EQ (client.entry_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.send_blocks, Qt::LeftButton);
    ASSERT_EQ (client.send_blocks_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.send_blocks_back, Qt::LeftButton);
    ASSERT_EQ (client.entry_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.settings, Qt::LeftButton);
    ASSERT_EQ (client.settings_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.settings_change_password_button, Qt::LeftButton);
    ASSERT_EQ (client.password_change.window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.password_change.back, Qt::LeftButton);
    ASSERT_EQ (client.settings_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.settings_back, Qt::LeftButton);
    ASSERT_EQ (client.entry_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.show_advanced, Qt::LeftButton);
    ASSERT_EQ (client.advanced.window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.advanced.show_ledger, Qt::LeftButton);
    ASSERT_EQ (client.advanced.ledger_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.advanced.ledger_back, Qt::LeftButton);
    ASSERT_EQ (client.advanced.window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.advanced.show_peers, Qt::LeftButton);
    ASSERT_EQ (client.advanced.peers_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.advanced.peers_back, Qt::LeftButton);
    ASSERT_EQ (client.advanced.window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.advanced.show_log, Qt::LeftButton);
    ASSERT_EQ (client.advanced.log_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.advanced.log_back, Qt::LeftButton);
    ASSERT_EQ (client.advanced.window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.advanced.back, Qt::LeftButton);
    ASSERT_EQ (client.entry_window, client.main_stack->currentWidget ());
}

TEST (client, password_change)
{
    rai::system system (24000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::client client (application, *system.clients [0]);
    QTest::mouseClick (client.settings, Qt::LeftButton);
    QTest::mouseClick (client.settings_change_password_button, Qt::LeftButton);
    ASSERT_NE (client.client_m.wallet.derive_key ("1"), client.client_m.wallet.password.value ());
    QTest::keyClicks (client.password_change.password, "1");
    QTest::keyClicks (client.password_change.retype, "1");
    QTest::mouseClick (client.password_change.change, Qt::LeftButton);
    ASSERT_EQ (client.client_m.wallet.derive_key ("1"), client.client_m.wallet.password.value ());
    ASSERT_EQ ("", client.password_change.password->text ());
    ASSERT_EQ ("", client.password_change.retype->text ());
}

TEST (client, password_nochange)
{
    rai::system system (24000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::client client (application, *system.clients [0]);
    QTest::mouseClick (client.settings, Qt::LeftButton);
    QTest::mouseClick (client.settings_change_password_button, Qt::LeftButton);
    ASSERT_EQ (client.client_m.wallet.derive_key (""), client.client_m.wallet.password.value ());
    QTest::keyClicks (client.password_change.password, "1");
    QTest::keyClicks (client.password_change.retype, "2");
    QTest::mouseClick (client.password_change.change, Qt::LeftButton);
    ASSERT_EQ (client.client_m.wallet.derive_key (""), client.client_m.wallet.password.value ());
    ASSERT_EQ ("1", client.password_change.password->text ());
    ASSERT_EQ ("2", client.password_change.retype->text ());
}

TEST (client, enter_password)
{
    rai::system system (24000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::client client (application, *system.clients [0]);
    ASSERT_NE (-1, client.enter_password.layout->indexOf (client.enter_password.valid));
    ASSERT_NE (-1, client.enter_password.layout->indexOf (client.enter_password.password));
    ASSERT_NE (-1, client.enter_password.layout->indexOf (client.enter_password.unlock));
    ASSERT_NE (-1, client.enter_password.layout->indexOf (client.enter_password.lock));
    ASSERT_NE (-1, client.enter_password.layout->indexOf (client.enter_password.back));
    ASSERT_FALSE (client.client_m.wallet.rekey ("abc"));
    QTest::mouseClick (client.settings, Qt::LeftButton);
    QTest::mouseClick (client.settings_enter_password_button, Qt::LeftButton);
    QTest::keyClicks (client.enter_password.password, "a");
    QTest::mouseClick (client.enter_password.unlock, Qt::LeftButton);
    ASSERT_EQ ("Password: INVALID", client.enter_password.valid->text ());
    client.enter_password.password->setText ("");
    QTest::keyClicks (client.enter_password.password, "abc");
    QTest::mouseClick (client.enter_password.unlock, Qt::LeftButton);
    ASSERT_EQ ("Password: Valid", client.enter_password.valid->text ());
    ASSERT_EQ ("", client.enter_password.password->text ());
}

TEST (client, send)
{
    rai::system system (24000, 2);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
    std::string account;
    key1.pub.encode_base58check (account);
    system.clients [1]->wallet.insert (key1.prv);
    int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::client client (application, *system.clients [0]);
    QTest::mouseClick (client.send_blocks, Qt::LeftButton);
    QTest::keyClicks (client.send_account, account.c_str ());
    QTest::keyClicks (client.send_count, "2");
    QTest::mouseClick (client.send_blocks_send, Qt::LeftButton);
    while (client.client_m.ledger.account_balance (key1.pub).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
    ASSERT_EQ (rai::scale_up (2), client.client_m.ledger.account_balance (key1.pub));
	QTest::mouseClick (client.send_blocks_back, Qt::LeftButton);
    QTest::mouseClick (client.show_advanced, Qt::LeftButton);
	QTest::mouseClick (client.advanced.show_ledger, Qt::LeftButton);
	QTest::mouseClick (client.advanced.ledger_refresh, Qt::LeftButton);
	ASSERT_EQ (2, client.advanced.ledger_model->rowCount ());
	ASSERT_EQ (3, client.advanced.ledger_model->columnCount ());
	auto item (client.advanced.ledger_model->itemFromIndex (client.advanced.ledger_model->index (1, 1)));
	ASSERT_EQ ("2", item->text ().toStdString ());
}


TEST (client, process_block)
{
    rai::system system (24000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
    rai_qt::client client (application, *system.clients [0]);
    ASSERT_EQ ("Process", client.block_entry.process->text ());
    ASSERT_EQ ("Back", client.block_entry.back->text ());
    rai::keypair key1;
    ASSERT_EQ (client.entry_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.show_advanced, Qt::LeftButton);
    QTest::mouseClick (client.advanced.enter_block, Qt::LeftButton);
    ASSERT_EQ (client.block_entry.window, client.main_stack->currentWidget ());
    rai::send_block send;
    send.hashables.destination = key1.pub;
    send.hashables.previous = system.clients [0]->ledger.latest (rai::genesis_account);
    send.hashables.balance = 0;
    send.work = system.clients [0]->ledger.create_work (send);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send.hash (), send.signature);
    std::string destination;
    send.hashables.destination.encode_hex (destination);
    std::string previous;
    send.hashables.previous.encode_hex (previous);
    std::string balance;
    send.hashables.balance.encode_hex (balance);
    std::string signature;
    send.signature.encode_hex (signature);
    auto block_json (boost::str (boost::format ("{\"type\": \"send\", \"previous\": \"%1%\", \"balance\": \"%2%\", \"destination\": \"%3%\", \"work\": \"%4%\", \"signature\": \"%5%\"}") % previous % balance % destination % rai::to_string_hex (send.work) % signature));
    QTest::keyClicks (client.block_entry.block, block_json.c_str ());
    QTest::mouseClick (client.block_entry.process, Qt::LeftButton);
    ASSERT_EQ (send.hash (), system.clients [0]->ledger.latest (rai::genesis_account));
    QTest::mouseClick(client.block_entry.back, Qt::LeftButton);
    ASSERT_EQ (client.advanced.window, client.main_stack->currentWidget ());
}