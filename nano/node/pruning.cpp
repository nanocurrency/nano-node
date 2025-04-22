#include <nano/node/node.hpp>
#include <nano/node/pruning.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>

nano::pruning::pruning (nano::node_config const & config_a, nano::node_flags const & flags_a, nano::ledger & ledger_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ config_a },
	flags{ flags_a },
	ledger{ ledger_a },
	stats{ stats_a },
	logger{ logger_a },
	workers{ /* single threaded */ 1, nano::thread_role::name::pruning }
{
}

nano::pruning::~pruning ()
{
	// Must be stopped before destruction
	debug_assert (stopped);
}

void nano::pruning::start ()
{
	if (flags.enable_pruning)
	{
		workers.start ();
		workers.post ([this] () {
			ongoing_ledger_pruning ();
		});
	}
}

void nano::pruning::stop ()
{
	stopped = true;
	workers.stop ();
}

void nano::pruning::ongoing_ledger_pruning ()
{
	if (stopped)
	{
		return;
	}

	auto bootstrap_weight_reached (ledger.block_count () >= ledger.bootstrap_weight_max_blocks);

	ledger_pruning (flags.block_processor_batch_size != 0 ? flags.block_processor_batch_size : 2 * 1024, bootstrap_weight_reached);

	auto const ledger_pruning_interval (bootstrap_weight_reached ? config.max_pruning_age : std::min (config.max_pruning_age, std::chrono::seconds (15 * 60)));
	logger.debug (nano::log::type::pruning, "Next pruning iteration in {}s", ledger_pruning_interval.count ());
	workers.post_delayed (ledger_pruning_interval, [this] () {
		ongoing_ledger_pruning ();
	});
}

void nano::pruning::ledger_pruning (uint64_t const batch_size_a, bool bootstrap_weight_reached_a)
{
	stats.inc (nano::stat::type::pruning, nano::stat::detail::ledger_pruning);

	uint64_t const max_depth (config.max_pruning_depth != 0 ? config.max_pruning_depth : std::numeric_limits<uint64_t>::max ());
	uint64_t const cutoff_time (bootstrap_weight_reached_a ? nano::seconds_since_epoch () - config.max_pruning_age.count () : std::numeric_limits<uint64_t>::max ());
	uint64_t pruned_count (0);
	uint64_t transaction_write_count (0);
	nano::account last_account (1); // 0 Burn account is never opened. So it can be used to break loop
	std::deque<nano::block_hash> pruning_targets;
	bool target_finished (false);
	while ((transaction_write_count != 0 || !target_finished) && !stopped)
	{
		// Search pruning targets
		while (pruning_targets.size () < batch_size_a && !target_finished && !stopped)
		{
			stats.inc (nano::stat::type::pruning, nano::stat::detail::collect_targets);
			target_finished = collect_ledger_pruning_targets (pruning_targets, last_account, batch_size_a * 2, max_depth, cutoff_time);
		}
		// Pruning write operation
		transaction_write_count = 0;
		if (!pruning_targets.empty () && !stopped)
		{
			auto write_transaction = ledger.tx_begin_write (nano::store::writer::pruning);
			while (!pruning_targets.empty () && transaction_write_count < batch_size_a && !stopped)
			{
				stats.inc (nano::stat::type::pruning, nano::stat::detail::pruning_target);

				auto const & pruning_hash (pruning_targets.front ());
				auto account_pruned_count (ledger.pruning_action (write_transaction, pruning_hash, batch_size_a));
				transaction_write_count += account_pruned_count;
				pruning_targets.pop_front ();

				stats.add (nano::stat::type::pruning, nano::stat::detail::pruned_count, account_pruned_count);
			}
			pruned_count += transaction_write_count;

			logger.debug (nano::log::type::pruning, "Pruned blocks: {}", pruned_count);
		}
	}

	logger.info (nano::log::type::pruning, "Recently pruned blocks: {}", pruned_count);
}

bool nano::pruning::collect_ledger_pruning_targets (std::deque<nano::block_hash> & pruning_targets_a, nano::account & last_account_a, uint64_t const batch_read_size_a, uint64_t const max_depth_a, uint64_t const cutoff_time_a)
{
	uint64_t read_operations (0);
	bool finish_transaction (false);
	auto transaction = ledger.tx_begin_read ();
	for (auto i (ledger.store.confirmation_height.begin (transaction, last_account_a)), n (ledger.store.confirmation_height.end (transaction)); i != n && !finish_transaction;)
	{
		++read_operations;
		auto const & account (i->first);
		nano::block_hash hash (i->second.frontier);
		uint64_t depth (0);
		while (!hash.is_zero () && depth < max_depth_a)
		{
			auto block = ledger.any.block_get (transaction, hash);
			if (block != nullptr)
			{
				if (block->sideband ().timestamp > cutoff_time_a || depth == 0)
				{
					hash = block->previous ();
				}
				else
				{
					break;
				}
			}
			else
			{
				release_assert (depth != 0);
				hash = 0;
			}
			++depth;
		}
		if (!hash.is_zero ())
		{
			pruning_targets_a.push_back (hash);
		}
		read_operations += depth;
		if (read_operations >= batch_read_size_a)
		{
			last_account_a = inc_sat (account.number ());
			finish_transaction = true;
		}
		else
		{
			++i;
		}
	}
	return !finish_transaction || last_account_a.is_zero ();
}

nano::container_info nano::pruning::container_info () const
{
	return workers.container_info ();
}