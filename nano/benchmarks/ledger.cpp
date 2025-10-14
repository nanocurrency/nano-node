#include <nano/lib/logging.hpp>
#include <nano/node/make_store.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/utility.hpp>
#include <nano/store/account.hpp>

#include <benchmark/benchmark.h>

// Expects live ledger in default location
// PLEASE NOTE: Make sure to purge disk cache between runs (`purge` command on macOS)
static void BM_ledger_iterate_accounts (benchmark::State & state)
{
	nano::logger logger;
	nano::stats stats{ logger };

	// Use live ledger
	nano::network_type network = nano::network_type::nano_live_network;
	nano::network_params network_params{ network };
	auto application_path = nano::working_path (network);

	auto store_impl{ nano::make_store (logger, application_path, network_params.ledger) };
	auto & store{ *store_impl };

	auto ledger_impl{ std::make_unique<nano::ledger> (store, network_params, stats, logger, nano::generate_cache_flags::all_disabled ()) };
	auto & ledger{ *ledger_impl };

	auto transaction = ledger.tx_begin_read ();
	nano::account current{ 0 };
	nano::account_info current_info;
	auto it = ledger.any.account_begin (transaction);
	auto end = ledger.any.account_end ();
	for (auto _ : state)
	{
		if (it != end)
		{
			current = it->first;
			current_info = it->second;
			benchmark::DoNotOptimize (current);
			benchmark::DoNotOptimize (current_info);

			++it;
		}
		else
		{
			break;
		}
	}
}
BENCHMARK (BM_ledger_iterate_accounts);

// Expects live ledger in default location
// PLEASE NOTE: Make sure to purge disk cache between runs (`purge` command on macOS)
static void BM_store_iterate_accounts (benchmark::State & state)
{
	nano::logger logger;
	nano::stats stats{ logger };

	// Use live ledger
	nano::network_type network = nano::network_type::nano_live_network;
	nano::network_params network_params{ network };
	nano::node_flags flags;
	auto application_path = nano::working_path (network);

	auto store_impl{ nano::make_store (logger, application_path, network_params.ledger) };
	auto & store{ *store_impl };

	auto transaction = store.tx_begin_read ();
	nano::account current{ 0 };
	nano::account_info current_info;
	auto it = store.account.begin (transaction);
	auto end = store.account.end (transaction);
	for (auto _ : state)
	{
		if (it != end)
		{
			current = it->first;
			current_info = it->second;
			benchmark::DoNotOptimize (current);
			benchmark::DoNotOptimize (current_info);

			++it;
		}
		else
		{
			break;
		}
	}
}
BENCHMARK (BM_store_iterate_accounts);