#include <rai/qt/qt.hpp>

#include <thread>

int main (int argc, char ** argv)
{
    QApplication application (argc, argv);
    static int count (16);
    rai::system system (24000, count);
    std::unique_ptr <QTabWidget> client_tabs (new QTabWidget);
    std::vector <std::unique_ptr <rai_qt::wallet>> guis;
    for (auto i (0); i < count; ++i)
    {
        rai::uint256_union wallet_id;
        rai::random_pool.GenerateBlock (wallet_id.bytes.data (), wallet_id.bytes.size ());
        auto wallet (system.nodes [i]->wallets.create (wallet_id));
        rai::keypair key;
		rai::transaction transaction (wallet->store.environment, nullptr, true);
        wallet->store.insert (transaction, key.prv);
        guis.push_back (std::unique_ptr <rai_qt::wallet> (new rai_qt::wallet (application, *system.nodes [i], wallet, key.pub)));
        client_tabs->addTab (guis.back ()->client_window, boost::str (boost::format ("Wallet %1%") % i).c_str ());
    }
    client_tabs->show ();
    std::thread network_thread ([&system] ()
    {
        try
        {
            system.service->run ();
        }
        catch (...)
        {
            assert (false);
        }
    });
    std::thread processor_thread ([&system] ()
    {
        try
        {
            system.processor.run ();
        }
        catch (...)
        {
            assert (false);
        }
    });
    QObject::connect (&application, &QApplication::aboutToQuit, [&] ()
    {
        for (auto & i: system.nodes)
        {
            i->stop ();
        }
        system.processor.stop ();
    });
    int result;
    try
    {
        result = application.exec ();
    }
    catch (...)
    {
        result = -1;
        assert (false);
    }
    network_thread.join ();
    processor_thread.join ();
    return result;
}