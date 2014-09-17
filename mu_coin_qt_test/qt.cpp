#include <gtest/gtest.h>

#include <mu_coin_qt/qt.hpp>
#include <thread>
#include <QTest>

TEST (client, construction)
{
    mu_coin::system system (1, 24000, 25000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
    mu_coin_qt::client client (application, *system.clients [0]);
}

TEST (client, main)
{
    mu_coin::system system (1, 24000, 25000, 1);
    int argc (0);
    QApplication application (argc, nullptr);
    mu_coin_qt::client client (application, *system.clients [0]);
    ASSERT_EQ (client.entry_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.send_coins, Qt::LeftButton);
    ASSERT_EQ (client.send_coins_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.send_coins_back, Qt::LeftButton);
    ASSERT_EQ (client.entry_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.show_wallet, Qt::LeftButton);
    ASSERT_EQ (client.wallet_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.wallet_back, Qt::LeftButton);
    ASSERT_EQ (client.entry_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.settings, Qt::LeftButton);
    ASSERT_EQ (client.settings_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.settings_back, Qt::LeftButton);
    ASSERT_EQ (client.entry_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.show_ledger, Qt::LeftButton);
    ASSERT_EQ (client.ledger_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.ledger_back, Qt::LeftButton);
    ASSERT_EQ (client.entry_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.show_peers, Qt::LeftButton);
    ASSERT_EQ (client.peers_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.peers_back, Qt::LeftButton);
    ASSERT_EQ (client.entry_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.show_log, Qt::LeftButton);
    ASSERT_EQ (client.log_window, client.main_stack->currentWidget ());
    QTest::mouseClick (client.log_back, Qt::LeftButton);
    ASSERT_EQ (client.entry_window, client.main_stack->currentWidget ());
}