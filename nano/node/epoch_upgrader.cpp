#include <nano/lib/blocks.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/epoch_upgrader.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>

nano::epoch_upgrader::epoch_upgrader (nano::node & node_a, nano::ledger & ledger_a, nano::store::component & store_a, nano::network_params & network_params_a, nano::logger & logger_a) :
	node{ node_a },
	ledger{ ledger_a },
	store{ store_a },
	network_params{ network_params_a },
	logger{ logger_a }
{
}

void nano::epoch_upgrader::stop ()
{
	stopped = true;

	auto epoch_upgrade = epoch_upgrading.lock ();
	if (epoch_upgrade->valid ())
	{
		epoch_upgrade->wait ();
	}
}

bool nano::epoch_upgrader::start (nano::raw_key const & prv_a, nano::epoch epoch_a, uint64_t count_limit, uint64_t threads)
{
	bool error = stopped.load ();
	if (!error)
	{
		auto epoch_upgrade = epoch_upgrading.lock ();
		error = epoch_upgrade->valid () && epoch_upgrade->wait_for (std::chrono::seconds (0)) == std::future_status::timeout;
		if (!error)
		{
			*epoch_upgrade = std::async (std::launch::async, [this, prv_a, epoch_a, count_limit, threads] () {
				upgrade_impl (prv_a, epoch_a, count_limit, threads);
			});
		}
	}
	return error;
}

// TODO: This method should be a class
void nano::epoch_upgrader::upgrade_impl (nano::raw_key const & prv_a, nano::epoch epoch_a, uint64_t count_limit, uint64_t threads)
{
	nano::thread_role::set (nano::thread_role::name::epoch_upgrader);
	auto upgrader_process = [this] (std::atomic<uint64_t> & counter, std::shared_ptr<nano::block> const & epoch, uint64_t difficulty, nano::public_key const & signer_a, nano::root const & root_a, nano::account const & account_a) {
		epoch->block_work_set (node.work_generate_blocking (nano::work_version::work_1, root_a, difficulty).value_or (0));
		bool valid_signature (!nano::validate_message (signer_a, epoch->hash (), epoch->block_signature ()));
		bool valid_work (node.network_params.work.difficulty (*epoch) >= difficulty);
		nano::block_status result (nano::block_status::old);
		if (valid_signature && valid_work)
		{
			result = node.process_local (epoch).value ();
		}
		if (result == nano::block_status::progress)
		{
			++counter;
		}
		else
		{
			bool fork (result == nano::block_status::fork);

			logger.error (nano::log::type::epoch_upgrader, "Failed to upgrade account {} (valid signature: {}, valid work: {}, fork: {})",
			account_a.to_account (),
			valid_signature,
			valid_work,
			fork);
		}
	};

	uint64_t const upgrade_batch_size = 1000;
	nano::block_builder builder;
	auto link (ledger.epoch_link (epoch_a));
	nano::raw_key raw_key;
	raw_key = prv_a;
	auto signer (nano::pub_key (prv_a));
	debug_assert (signer == ledger.epoch_signer (link));

	nano::mutex upgrader_mutex;
	nano::condition_variable upgrader_condition;

	class account_upgrade_item final
	{
	public:
		nano::account account{};
		uint64_t modified{ 0 };
	};
	class account_tag
	{
	};
	class modified_tag
	{
	};
	// clang-format off
	boost::multi_index_container<account_upgrade_item,
	boost::multi_index::indexed_by<
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<modified_tag>,
			boost::multi_index::member<account_upgrade_item, uint64_t, &account_upgrade_item::modified>,
			std::greater<uint64_t>>,
		boost::multi_index::hashed_unique<boost::multi_index::tag<account_tag>,
			boost::multi_index::member<account_upgrade_item, nano::account, &account_upgrade_item::account>>>>
	accounts_list;
	// clang-format on

	bool finished_upgrade (false);

	while (!finished_upgrade && !stopped)
	{
		bool finished_accounts (false);
		uint64_t total_upgraded_accounts (0);
		while (!finished_accounts && count_limit != 0 && !stopped)
		{
			{
				auto transaction (store.tx_begin_read ());
				// Collect accounts to upgrade
				for (auto i (store.account.begin (transaction)), n (store.account.end ()); i != n && accounts_list.size () < count_limit; ++i)
				{
					nano::account const & account (i->first);
					nano::account_info const & info (i->second);
					if (info.epoch () < epoch_a)
					{
						release_assert (nano::epochs::is_sequential (info.epoch (), epoch_a));
						accounts_list.emplace (account_upgrade_item{ account, info.modified });
					}
				}
			}

			/* Upgrade accounts
			Repeat until accounts with previous epoch exist in latest table */
			std::atomic<uint64_t> upgraded_accounts (0);
			uint64_t workers (0);
			uint64_t attempts (0);
			for (auto i (accounts_list.get<modified_tag> ().begin ()), n (accounts_list.get<modified_tag> ().end ()); i != n && attempts < upgrade_batch_size && attempts < count_limit && !stopped; ++i)
			{
				nano::account const & account (i->account);
				auto info = ledger.any.account_get (ledger.tx_begin_read (), account);
				if (info && info->epoch () < epoch_a)
				{
					++attempts;
					auto difficulty (node.network_params.work.threshold (nano::work_version::work_1, nano::block_details (epoch_a, false, false, true)));
					nano::root const & root (info->head);
					std::shared_ptr<nano::block> epoch = builder.state ()
														 .account (account)
														 .previous (info->head)
														 .representative (info->representative)
														 .balance (info->balance)
														 .link (link)
														 .sign (raw_key, signer)
														 .work (0)
														 .build ();
					if (threads != 0)
					{
						{
							nano::unique_lock<nano::mutex> lock{ upgrader_mutex };
							++workers;
							while (workers > threads)
							{
								upgrader_condition.wait (lock);
							}
						}
						node.workers.post ([&upgrader_process, &upgrader_mutex, &upgrader_condition, &upgraded_accounts, &workers, epoch, difficulty, signer, root, account] () {
							upgrader_process (upgraded_accounts, epoch, difficulty, signer, root, account);
							{
								nano::lock_guard<nano::mutex> lock{ upgrader_mutex };
								--workers;
							}
							upgrader_condition.notify_all ();
						});
					}
					else
					{
						upgrader_process (upgraded_accounts, epoch, difficulty, signer, root, account);
					}
				}
			}
			{
				nano::unique_lock<nano::mutex> lock{ upgrader_mutex };
				while (workers > 0)
				{
					upgrader_condition.wait (lock);
				}
			}
			total_upgraded_accounts += upgraded_accounts;
			count_limit -= upgraded_accounts;

			if (!accounts_list.empty ())
			{
				logger.info (nano::log::type::epoch_upgrader, "{} accounts were upgraded to new epoch, {} remain...",
				total_upgraded_accounts,
				accounts_list.size () - upgraded_accounts);

				accounts_list.clear ();
			}
			else
			{
				logger.info (nano::log::type::epoch_upgrader, "{} total accounts were upgraded to new epoch", total_upgraded_accounts);

				finished_accounts = true;
			}
		}

		// Pending blocks upgrade
		bool finished_pending (false);
		uint64_t total_upgraded_pending (0);
		while (!finished_pending && count_limit != 0 && !stopped)
		{
			std::atomic<uint64_t> upgraded_pending (0);
			uint64_t workers (0);
			uint64_t attempts (0);
			for (auto current = ledger.any.receivable_upper_bound (ledger.tx_begin_read (), 0), end = ledger.any.receivable_end (); current != end && attempts < upgrade_batch_size && attempts < count_limit && !stopped;)
			{
				auto const & [key, info] = *current;
				if (!store.account.exists (ledger.tx_begin_read (), key.account))
				{
					if (info.epoch < epoch_a)
					{
						++attempts;
						release_assert (nano::epochs::is_sequential (info.epoch, epoch_a));
						auto difficulty (network_params.work.threshold (nano::work_version::work_1, nano::block_details (epoch_a, false, false, true)));
						nano::root const & root (key.account);
						nano::account const & account (key.account);
						std::shared_ptr<nano::block> epoch = builder.state ()
															 .account (key.account)
															 .previous (0)
															 .representative (0)
															 .balance (0)
															 .link (link)
															 .sign (raw_key, signer)
															 .work (0)
															 .build ();
						if (threads != 0)
						{
							{
								nano::unique_lock<nano::mutex> lock{ upgrader_mutex };
								++workers;
								while (workers > threads)
								{
									upgrader_condition.wait (lock);
								}
							}
							node.workers.post ([&upgrader_process, &upgrader_mutex, &upgrader_condition, &upgraded_pending, &workers, epoch, difficulty, signer, root, account] () {
								upgrader_process (upgraded_pending, epoch, difficulty, signer, root, account);
								{
									nano::lock_guard<nano::mutex> lock{ upgrader_mutex };
									--workers;
								}
								upgrader_condition.notify_all ();
							});
						}
						else
						{
							upgrader_process (upgraded_pending, epoch, difficulty, signer, root, account);
						}
					}
					// Move to next pending item
					current = ledger.any.receivable_upper_bound (ledger.tx_begin_read (), key.account, key.hash);
				}
				else
				{
					// Move to next account if pending account exists or was upgraded
					if (key.account.number () == std::numeric_limits<nano::uint256_t>::max ())
					{
						break;
					}
					else
					{
						current = ledger.any.receivable_upper_bound (ledger.tx_begin_read (), key.account);
					}
				}
			}
			{
				nano::unique_lock<nano::mutex> lock{ upgrader_mutex };
				while (workers > 0)
				{
					upgrader_condition.wait (lock);
				}
			}

			total_upgraded_pending += upgraded_pending;
			count_limit -= upgraded_pending;

			// Repeat if some pending accounts were upgraded
			if (upgraded_pending != 0)
			{
				logger.info (nano::log::type::epoch_upgrader, "{} unopened accounts with pending blocks were upgraded to new epoch...", total_upgraded_pending);
			}
			else
			{
				logger.info (nano::log::type::epoch_upgrader, "{} total unopened accounts with pending blocks were upgraded to new epoch", total_upgraded_pending);

				finished_pending = true;
			}
		}

		finished_upgrade = (total_upgraded_accounts == 0) && (total_upgraded_pending == 0);
	}

	logger.info (nano::log::type::epoch_upgrader, "Epoch upgrade is completed");
}
