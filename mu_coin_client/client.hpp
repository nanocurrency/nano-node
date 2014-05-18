#pragma once

#include <mu_coin_store/db.hpp>
#include <mu_coin_wallet/wallet.hpp>
#include <QtGui>
#include <QtWidgets>

namespace mu_coin_client {
    class client : QObject
    {
    public:
        client (int, char **);
        ~client ();
        mu_coin_store::block_store_db store;
        mu_coin_wallet::wallet wallet;
        QApplication application;
        QMainWindow main_window;
        QStringListModel wallet_model;
        QListView wallet_view;
        QPushButton add_key;
    private:
        QStringList keys;
    private:
        void refresh_wallet ();
        void handle_add_key ();
    };
}