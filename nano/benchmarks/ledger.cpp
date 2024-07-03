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
	nano::logger::initialize_dummy ();
	nano::logger logger;
	nano::stats stats{ logger };

	// Use live ledger
	nano::networks network = nano::networks::nano_live_network;
	nano::network_params network_params{ network };
	nano::node_config config{ network_params };
	config.rocksdb_config.enable = state.range (0);
	auto application_path = nano::working_path (network);

	auto store_impl{ nano::make_store (logger, application_path, network_params.ledger, false, true, config.rocksdb_config, config.diagnostics_config.txn_tracking, config.block_processor_batch_max_time, config.lmdb_config, config.backup_before_upgrade) };
	auto & store{ *store_impl };

	if (store.init_error ())
	{
		state.SkipWithError ("Store initialization failed. Make sure ledger files are present in the default location.");
	}

	auto ledger_impl{ std::make_unique<nano::ledger> (store, stats, network_params.ledger, nano::generate_cache_flags::all_disabled (), config.representative_vote_weight_minimum.number ()) };
	auto & ledger{ *ledger_impl };

	auto transaction = ledger.tx_begin_read ();
	nano::account current{ 0 };
	nano::account_info current_info;
	auto it = ledger.any.account_begin (transaction);
	for (auto _ : state)
	{
		if (it != ledger.any.account_end ())
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
BENCHMARK (BM_ledger_iterate_accounts)->ArgName ("use_rocksdb")->Arg (0)->Arg (1);

// Expects live ledger in default location
// PLEASE NOTE: Make sure to purge disk cache between runs (`purge` command on macOS)
static void BM_store_iterate_accounts (benchmark::State & state)
{
	nano::logger::initialize_dummy ();
	nano::logger logger;
	nano::stats stats{ logger };

	// Use live ledger
	nano::networks network = nano::networks::nano_live_network;
	nano::network_params network_params{ network };
	nano::node_config config{ network_params };
	config.rocksdb_config.enable = state.range (0);
	nano::node_flags flags;
	auto application_path = nano::working_path (network);

	auto store_impl{ nano::make_store (logger, application_path, network_params.ledger, false, true, config.rocksdb_config, config.diagnostics_config.txn_tracking, config.block_processor_batch_max_time, config.lmdb_config, config.backup_before_upgrade, flags.force_use_write_queue) };
	auto & store{ *store_impl };

	if (store.init_error ())
	{
		state.SkipWithError ("Store initialization failed. Make sure ledger files are present in the default location.");
	}

	auto transaction = store.tx_begin_read ();
	nano::account current{ 0 };
	nano::account_info current_info;
	auto it = store.account.begin (transaction);
	for (auto _ : state)
	{
		if (it != store.account.end ())
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
BENCHMARK (BM_store_iterate_accounts)->ArgName ("use_rocksdb")->Arg (0)->Arg (1);