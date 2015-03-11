#include <gtest/gtest.h>
#include <rai/node.hpp>

#include <thread>

TEST (system, generate_mass_activity)
{
    rai::system system (24000, 1);
    system.wallet (0)->insert (rai::test_genesis_key.prv);
    size_t count (20);
    system.generate_mass_activity (count, *system.nodes [0]);
    size_t accounts (0);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    for (auto i (system.nodes [0]->store.latest_begin (transaction)), n (system.nodes [0]->store.latest_end ()); i != n; ++i)
    {
        ++accounts;
    }
    ASSERT_GT (accounts, count / 10);
}

TEST (system, generate_mass_activity_long)
{
    rai::system system (24000, 1);
	std::vector <std::thread> threads;
	for (auto i (0), n (4); i != n; ++i)
	{
		threads.push_back (std::thread ([&system] ()
		{
			system.service->run ();
		}));
	}
	for (auto i (0), n (4); i != n; ++i)
	{
		threads.push_back (std::thread ([&system] ()
		{
			system.processor.run ();
		}));
	}
    system.wallet (0)->insert (rai::test_genesis_key.prv);
    size_t count (10000);
    system.generate_mass_activity (count, *system.nodes [0]);
    size_t accounts (0);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    for (auto i (system.nodes [0]->store.latest_begin (transaction)), n (system.nodes [0]->store.latest_end ()); i != n; ++i)
    {
        ++accounts;
    }
    ASSERT_GT (accounts, count / 10);
}