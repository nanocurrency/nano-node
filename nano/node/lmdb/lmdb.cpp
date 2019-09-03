#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/common.hpp>
#include <nano/node/lmdb/lmdb.hpp>
#include <nano/node/lmdb/lmdb_iterator.hpp>
#include <nano/node/lmdb/wallet_value.hpp>
#include <nano/secure/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

#include <queue>

namespace nano
{
template <>
void * mdb_val::data () const
{
	return value.mv_data;
}

template <>
size_t mdb_val::size () const
{
	return value.mv_size;
}

template <>
mdb_val::db_val (size_t size_a, void * data_a, nano::epoch epoch_a) :
value ({ size_a, data_a }),
epoch (epoch_a)
{
}

template <>
void mdb_val::convert_buffer_to_value ()
{
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}
}

nano::mdb_store::mdb_store (nano::logger_mt & logger_a, boost::filesystem::path const & path_a, nano::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a, int lmdb_max_dbs, size_t const batch_size, bool backup_before_upgrade) :
logger (logger_a),
env (error, path_a, lmdb_max_dbs, true),
mdb_txn_tracker (logger_a, txn_tracking_config_a, block_processor_batch_max_time_a),
txn_tracking_enabled (txn_tracking_config_a.enable)
{
	if (!error)
	{
		auto is_fully_upgraded (false);
		{
			auto transaction (tx_begin_read ());
			auto err = mdb_dbi_open (env.tx (transaction), "meta", 0, &meta);
			if (err == MDB_SUCCESS)
			{
				is_fully_upgraded = (version_get (transaction) == version);
				mdb_dbi_close (env, meta);
			}
		}

		// Only open a write lock when upgrades are needed. This is because CLI commands
		// open inactive nodes which can otherwise be locked here if there is a long write
		// (can be a few minutes with the --fast_bootstrap flag for instance)
		if (!is_fully_upgraded)
		{
			if (backup_before_upgrade)
			{
				create_backup_file (env, path_a, logger_a);
			}
			auto transaction (tx_begin_write ());
			open_databases (error, transaction, MDB_CREATE);
			if (!error)
			{
				error |= do_upgrades (transaction, batch_size);
			}
		}
		else
		{
			auto transaction (tx_begin_read ());
			open_databases (error, transaction, 0);
		}
	}
}

void nano::mdb_store::serialize_mdb_tracker (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time)
{
	mdb_txn_tracker.serialize_json (json, min_read_time, min_write_time);
}

nano::write_transaction nano::mdb_store::tx_begin_write (std::vector<nano::tables> const &, std::vector<nano::tables> const &)
{
	return env.tx_begin_write (create_txn_callbacks ());
}

nano::read_transaction nano::mdb_store::tx_begin_read ()
{
	return env.tx_begin_read (create_txn_callbacks ());
}

nano::mdb_txn_callbacks nano::mdb_store::create_txn_callbacks ()
{
	nano::mdb_txn_callbacks mdb_txn_callbacks;
	if (txn_tracking_enabled)
	{
		// clang-format off
		mdb_txn_callbacks.txn_start = ([&mdb_txn_tracker = mdb_txn_tracker](const nano::transaction_impl * transaction_impl) {
			mdb_txn_tracker.add (transaction_impl);
		});
		mdb_txn_callbacks.txn_end = ([&mdb_txn_tracker = mdb_txn_tracker](const nano::transaction_impl * transaction_impl) {
			mdb_txn_tracker.erase (transaction_impl);
		});
		// clang-format on
	}
	return mdb_txn_callbacks;
}

void nano::mdb_store::open_databases (bool & error_a, nano::transaction const & transaction_a, unsigned flags)
{
	error_a |= mdb_dbi_open (env.tx (transaction_a), "frontiers", flags, &frontiers) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "accounts", flags, &accounts_v0) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "accounts_v1", flags, &accounts_v1) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "send", flags, &send_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "receive", flags, &receive_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "open", flags, &open_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "change", flags, &change_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "state", flags, &state_blocks_v0) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "state_v1", flags, &state_blocks_v1) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "pending", flags, &pending_v0) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "pending_v1", flags, &pending_v1) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "unchecked", flags, &unchecked) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "vote", flags, &vote) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "online_weight", flags, &online_weight) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "meta", flags, &meta) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "peers", flags, &peers) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "confirmation_height", flags, &confirmation_height) != 0;
	if (!full_sideband (transaction_a))
	{
		error_a |= mdb_dbi_open (env.tx (transaction_a), "blocks_info", flags, &blocks_info) != 0;
	}
}

bool nano::mdb_store::do_upgrades (nano::write_transaction & transaction_a, size_t batch_size)
{
	auto error (false);
	auto version_l = version_get (transaction_a);
	switch (version_l)
	{
		case 1:
			upgrade_v1_to_v2 (transaction_a);
		case 2:
			upgrade_v2_to_v3 (transaction_a);
		case 3:
			upgrade_v3_to_v4 (transaction_a);
		case 4:
			upgrade_v4_to_v5 (transaction_a);
		case 5:
			upgrade_v5_to_v6 (transaction_a);
		case 6:
			upgrade_v6_to_v7 (transaction_a);
		case 7:
			upgrade_v7_to_v8 (transaction_a);
		case 8:
			upgrade_v8_to_v9 (transaction_a);
		case 9:
		case 10:
			upgrade_v10_to_v11 (transaction_a);
		case 11:
			upgrade_v11_to_v12 (transaction_a);
		case 12:
			upgrade_v12_to_v13 (transaction_a, batch_size);
		case 13:
			upgrade_v13_to_v14 (transaction_a);
		case 14:
			upgrade_v14_to_v15 (transaction_a);
		case 15:
			break;
		default:
			logger.always_log (boost::str (boost::format ("The version of the ledger (%1%) is too high for this node") % version_l));
			error = true;
			break;
	}
	return error;
}

void nano::mdb_store::upgrade_v1_to_v2 (nano::write_transaction const & transaction_a)
{
	version_put (transaction_a, 2);
	nano::account account (1);
	while (!account.is_zero ())
	{
		nano::mdb_iterator<nano::uint256_union, nano::account_info_v1> i (transaction_a, accounts_v0, nano::mdb_val (account));
		std::cerr << std::hex;
		if (i != nano::mdb_iterator<nano::uint256_union, nano::account_info_v1> (nullptr))
		{
			account = nano::uint256_union (i->first);
			nano::account_info_v1 v1 (i->second);
			nano::account_info_v5 v2;
			v2.balance = v1.balance;
			v2.head = v1.head;
			v2.modified = v1.modified;
			v2.rep_block = v1.rep_block;
			auto block (block_get (transaction_a, v1.head));
			while (!block->previous ().is_zero ())
			{
				block = block_get (transaction_a, block->previous ());
			}
			v2.open_block = block->hash ();
			auto status (mdb_put (env.tx (transaction_a), accounts_v0, nano::mdb_val (account), nano::mdb_val (sizeof (v2), &v2), 0));
			release_assert (status == 0);
			account = account.number () + 1;
		}
		else
		{
			account.clear ();
		}
	}
}

void nano::mdb_store::upgrade_v2_to_v3 (nano::write_transaction const & transaction_a)
{
	version_put (transaction_a, 3);
	mdb_drop (env.tx (transaction_a), representation, 0);
	for (auto i (std::make_unique<nano::mdb_iterator<nano::account, nano::account_info_v5>> (transaction_a, accounts_v0)), n (std::make_unique<nano::mdb_iterator<nano::account, nano::account_info_v5>> (nullptr)); *i != *n; ++(*i))
	{
		nano::account account_l ((*i)->first);
		nano::account_info_v5 info ((*i)->second);
		representative_visitor visitor (transaction_a, *this);
		visitor.compute (info.head);
		assert (!visitor.result.is_zero ());
		info.rep_block = visitor.result;
		auto impl (boost::polymorphic_downcast<nano::mdb_iterator<nano::account, nano::account_info_v5> *> (i.get ()));
		mdb_cursor_put (impl->cursor, nano::mdb_val (account_l), nano::mdb_val (sizeof (info), &info), MDB_CURRENT);
	}
}

void nano::mdb_store::upgrade_v3_to_v4 (nano::write_transaction const & transaction_a)
{
	version_put (transaction_a, 4);
	std::queue<std::pair<nano::pending_key, nano::pending_info>> items;
	for (auto i (nano::store_iterator<nano::block_hash, nano::pending_info_v3> (std::make_unique<nano::mdb_iterator<nano::block_hash, nano::pending_info_v3>> (transaction_a, pending_v0))), n (nano::store_iterator<nano::block_hash, nano::pending_info_v3> (nullptr)); i != n; ++i)
	{
		nano::block_hash const & hash (i->first);
		nano::pending_info_v3 const & info (i->second);
		items.push (std::make_pair (nano::pending_key (info.destination, hash), nano::pending_info (info.source, info.amount, nano::epoch::epoch_0)));
	}
	mdb_drop (env.tx (transaction_a), pending_v0, 0);
	while (!items.empty ())
	{
		pending_put (transaction_a, items.front ().first, items.front ().second);
		items.pop ();
	}
}

void nano::mdb_store::upgrade_v4_to_v5 (nano::write_transaction const & transaction_a)
{
	version_put (transaction_a, 5);
	for (auto i (nano::store_iterator<nano::account, nano::account_info_v5> (std::make_unique<nano::mdb_iterator<nano::account, nano::account_info_v5>> (transaction_a, accounts_v0))), n (nano::store_iterator<nano::account, nano::account_info_v5> (nullptr)); i != n; ++i)
	{
		nano::account_info_v5 const & info (i->second);
		nano::block_hash successor (0);
		auto block (block_get (transaction_a, info.head));
		while (block != nullptr)
		{
			auto hash (block->hash ());
			if (block_successor (transaction_a, hash).is_zero () && !successor.is_zero ())
			{
				std::vector<uint8_t> vector;
				{
					nano::vectorstream stream (vector);
					block->serialize (stream);
					nano::write (stream, successor.bytes);
				}
				block_raw_put (transaction_a, vector, block->type (), nano::epoch::epoch_0, hash);
				if (!block->previous ().is_zero ())
				{
					nano::block_type type;
					auto value (block_raw_get (transaction_a, block->previous (), type));
					auto version (block_version (transaction_a, block->previous ()));
					assert (value.size () != 0);
					std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
					std::copy (hash.bytes.begin (), hash.bytes.end (), data.end () - nano::block_sideband::size (type));
					block_raw_put (transaction_a, data, type, version, block->previous ());
				}
			}
			successor = hash;
			block = block_get (transaction_a, block->previous ());
		}
	}
}

void nano::mdb_store::upgrade_v5_to_v6 (nano::write_transaction const & transaction_a)
{
	version_put (transaction_a, 6);
	std::deque<std::pair<nano::account, nano::account_info_v13>> headers;
	for (auto i (nano::store_iterator<nano::account, nano::account_info_v5> (std::make_unique<nano::mdb_iterator<nano::account, nano::account_info_v5>> (transaction_a, accounts_v0))), n (nano::store_iterator<nano::account, nano::account_info_v5> (nullptr)); i != n; ++i)
	{
		nano::account const & account (i->first);
		nano::account_info_v5 info_old (i->second);
		uint64_t block_count (0);
		auto hash (info_old.head);
		while (!hash.is_zero ())
		{
			++block_count;
			auto block (block_get (transaction_a, hash));
			assert (block != nullptr);
			hash = block->previous ();
		}
		headers.emplace_back (account, nano::account_info_v13{ info_old.head, info_old.rep_block, info_old.open_block, info_old.balance, info_old.modified, block_count, nano::epoch::epoch_0 });
	}
	for (auto i (headers.begin ()), n (headers.end ()); i != n; ++i)
	{
		auto status (mdb_put (env.tx (transaction_a), accounts_v0, nano::mdb_val (i->first), nano::mdb_val (i->second), 0));
		release_assert (status == 0);
	}
}

void nano::mdb_store::upgrade_v6_to_v7 (nano::write_transaction const & transaction_a)
{
	version_put (transaction_a, 7);
	mdb_drop (env.tx (transaction_a), unchecked, 0);
}

void nano::mdb_store::upgrade_v7_to_v8 (nano::write_transaction const & transaction_a)
{
	version_put (transaction_a, 8);
	mdb_drop (env.tx (transaction_a), unchecked, 1);
	mdb_dbi_open (env.tx (transaction_a), "unchecked", MDB_CREATE | MDB_DUPSORT, &unchecked);
}

void nano::mdb_store::upgrade_v8_to_v9 (nano::write_transaction const & transaction_a)
{
	version_put (transaction_a, 9);
	MDB_dbi sequence;
	mdb_dbi_open (env.tx (transaction_a), "sequence", MDB_CREATE | MDB_DUPSORT, &sequence);
	nano::genesis genesis;
	std::shared_ptr<nano::block> block (std::move (genesis.open));
	nano::keypair junk;
	for (nano::mdb_iterator<nano::account, uint64_t> i (transaction_a, sequence), n (nano::mdb_iterator<nano::account, uint64_t> (nullptr)); i != n; ++i)
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
		uint64_t sequence;
		auto error (nano::try_read (stream, sequence));
		(void)error;
		// Create a dummy vote with the same sequence number for easy upgrading.  This won't have a valid signature.
		nano::vote dummy (nano::account (i->first), junk.prv, sequence, block);
		std::vector<uint8_t> vector;
		{
			nano::vectorstream stream (vector);
			dummy.serialize (stream);
		}
		auto status1 (mdb_put (env.tx (transaction_a), vote, nano::mdb_val (i->first), nano::mdb_val (vector.size (), vector.data ()), 0));
		release_assert (status1 == 0);
		assert (!error);
	}
	mdb_drop (env.tx (transaction_a), sequence, 1);
}

void nano::mdb_store::upgrade_v10_to_v11 (nano::write_transaction const & transaction_a)
{
	version_put (transaction_a, 11);
	MDB_dbi unsynced;
	mdb_dbi_open (env.tx (transaction_a), "unsynced", MDB_CREATE | MDB_DUPSORT, &unsynced);
	mdb_drop (env.tx (transaction_a), unsynced, 1);
}

void nano::mdb_store::upgrade_v11_to_v12 (nano::write_transaction const & transaction_a)
{
	version_put (transaction_a, 12);
	mdb_drop (env.tx (transaction_a), unchecked, 1);
	mdb_dbi_open (env.tx (transaction_a), "unchecked", MDB_CREATE, &unchecked);
	MDB_dbi checksum;
	mdb_dbi_open (env.tx (transaction_a), "checksum", MDB_CREATE, &checksum);
	mdb_drop (env.tx (transaction_a), checksum, 1);
}

void nano::mdb_store::upgrade_v12_to_v13 (nano::write_transaction & transaction_a, size_t const batch_size)
{
	size_t cost (0);
	nano::account account (0);
	auto const & not_an_account (network_params.random.not_an_account);
	while (account != not_an_account)
	{
		nano::account first (0);
		nano::account_info_v13 second;
		{
			nano::store_iterator<nano::account, nano::account_info_v13> current (std::make_unique<nano::mdb_merge_iterator<nano::account, nano::account_info_v13>> (transaction_a, accounts_v0, accounts_v1, nano::mdb_val (account)));
			nano::store_iterator<nano::account, nano::account_info_v13> end (nullptr);
			if (current != end)
			{
				first = current->first;
				second = current->second;
			}
		}
		if (!first.is_zero ())
		{
			auto hash (second.open_block);
			uint64_t height (1);
			nano::block_sideband sideband;
			while (!hash.is_zero ())
			{
				if (cost >= batch_size)
				{
					logger.always_log (boost::str (boost::format ("Upgrading sideband information for account %1%... height %2%") % first.to_account ().substr (0, 24) % std::to_string (height)));
					transaction_a.commit ();
					std::this_thread::yield ();
					transaction_a.renew ();
					cost = 0;
				}
				auto block (block_get (transaction_a, hash, &sideband));
				assert (block != nullptr);
				if (sideband.height == 0)
				{
					sideband.height = height;
					block_put (transaction_a, hash, *block, sideband, block_version (transaction_a, hash));
					cost += 16;
				}
				else
				{
					cost += 1;
				}
				hash = sideband.successor;
				++height;
			}
			account = first.number () + 1;
		}
		else
		{
			account = not_an_account;
		}
	}
	if (account == not_an_account)
	{
		logger.always_log ("Completed sideband upgrade");
		version_put (transaction_a, 13);
	}
}

void nano::mdb_store::upgrade_v13_to_v14 (nano::write_transaction const & transaction_a)
{
	// Upgrade all accounts to have a confirmation of 0 (except genesis which should have 1)
	version_put (transaction_a, 14);
	nano::store_iterator<nano::account, nano::account_info_v13> i (std::make_unique<nano::mdb_merge_iterator<nano::account, nano::account_info_v13>> (transaction_a, accounts_v0, accounts_v1));
	nano::store_iterator<nano::account, nano::account_info_v13> n (nullptr);

	std::vector<std::pair<nano::account, nano::account_info_v14>> account_infos;
	account_infos.reserve (account_count (transaction_a));
	for (; i != n; ++i)
	{
		nano::account_info_v13 const & account_info_v13 (i->second);
		uint64_t confirmation_height = 0;
		if (i->first == network_params.ledger.genesis_account)
		{
			confirmation_height = 1;
		}
		account_infos.emplace_back (i->first, nano::account_info_v14{ account_info_v13.head, account_info_v13.rep_block, account_info_v13.open_block, account_info_v13.balance, account_info_v13.modified, account_info_v13.block_count, confirmation_height, account_info_v13.epoch });
	}

	for (auto const & account_info : account_infos)
	{
		auto status1 (mdb_put (env.tx (transaction_a), table_to_dbi (get_account_db (account_info.second.epoch)), nano::mdb_val (account_info.first), nano::mdb_val (account_info.second), 0));
		release_assert (status1 == 0);
	}

	logger.always_log ("Completed confirmation height upgrade");

	nano::uint256_union node_id_mdb_key (3);
	auto error (mdb_del (env.tx (transaction_a), meta, nano::mdb_val (node_id_mdb_key), nullptr));
	release_assert (!error || error == MDB_NOTFOUND);
}

void nano::mdb_store::upgrade_v14_to_v15 (nano::write_transaction const & transaction_a)
{
	version_put (transaction_a, 15);

	// Move confirmation height from account_info database to its own table
	std::vector<std::pair<nano::account, nano::account_info>> account_infos;
	account_infos.reserve (account_count (transaction_a));

	nano::store_iterator<nano::account, nano::account_info_v14> i (std::make_unique<nano::mdb_merge_iterator<nano::account, nano::account_info_v14>> (transaction_a, accounts_v0, accounts_v1));
	nano::store_iterator<nano::account, nano::account_info_v14> n (nullptr);
	for (; i != n; ++i)
	{
		auto const & account_info_v14 (i->second);
		account_infos.emplace_back (i->first, nano::account_info{ account_info_v14.head, account_info_v14.rep_block, account_info_v14.open_block, account_info_v14.epoch });
		confirmation_height_put (transaction_a, i->first, i->second.confirmation_height);
	}

	for (auto const & account_info : account_infos)
	{
		account_put (transaction_a, account_info.first, account_info.second);
	}

	// Representation table is no longer used
	if (representation != 0)
	{
		auto status (mdb_drop (env.tx (transaction_a), representation, 1));
		release_assert (status == MDB_SUCCESS);
		representation = 0;
	}
}

/** Takes a filepath, appends '_backup_<timestamp>' to the end (but before any extension) and saves that file in the same directory */
void nano::mdb_store::create_backup_file (nano::mdb_env & env_a, boost::filesystem::path const & filepath_a, nano::logger_mt & logger_a)
{
	auto extension = filepath_a.extension ();
	auto filename_without_extension = filepath_a.filename ().replace_extension ("");
	auto orig_filepath = filepath_a;
	auto & backup_path = orig_filepath.remove_filename ();
	auto backup_filename = filename_without_extension;
	backup_filename += "_backup_";
	backup_filename += std::to_string (std::chrono::system_clock::now ().time_since_epoch ().count ());
	backup_filename += extension;
	auto backup_filepath = backup_path / backup_filename;
	auto start_message (boost::str (boost::format ("Performing %1% backup before database upgrade...") % filepath_a.filename ()));
	logger_a.always_log (start_message);
	std::cout << start_message << std::endl;
	auto error (mdb_env_copy (env_a, backup_filepath.string ().c_str ()));
	if (error)
	{
		auto error_message (boost::str (boost::format ("%1% backup failed") % filepath_a.filename ()));
		logger_a.always_log (error_message);
		std::cerr << error_message << std::endl;
		std::exit (1);
	}
	else
	{
		auto success_message (boost::str (boost::format ("Backup created: %1%") % backup_filename));
		logger_a.always_log (success_message);
		std::cout << success_message << std::endl;
	}
}

void nano::mdb_store::version_put (nano::write_transaction const & transaction_a, int version_a)
{
	nano::uint256_union version_key (1);
	nano::uint256_union version_value (version_a);
	auto status (mdb_put (env.tx (transaction_a), meta, nano::mdb_val (version_key), nano::mdb_val (version_value), 0));
	release_assert (status == 0);
	if (blocks_info == 0 && !full_sideband (transaction_a))
	{
		auto status (mdb_dbi_open (env.tx (transaction_a), "blocks_info", MDB_CREATE, &blocks_info));
		release_assert (status == MDB_SUCCESS);
	}
	if (blocks_info != 0 && full_sideband (transaction_a))
	{
		auto status (mdb_drop (env.tx (transaction_a), blocks_info, 1));
		release_assert (status == MDB_SUCCESS);
		blocks_info = 0;
	}
}

bool nano::mdb_store::block_info_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_info & block_info_a) const
{
	assert (!full_sideband (transaction_a));
	nano::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), blocks_info, nano::mdb_val (hash_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	bool result (true);
	if (status != MDB_NOTFOUND)
	{
		result = false;
		assert (value.size () == sizeof (block_info_a.account.bytes) + sizeof (block_info_a.balance.bytes));
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error1 (nano::try_read (stream, block_info_a.account));
		(void)error1;
		assert (!error1);
		auto error2 (nano::try_read (stream, block_info_a.balance));
		(void)error2;
		assert (!error2);
	}
	return result;
}

bool nano::mdb_store::exists (nano::transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a) const
{
	nano::mdb_val junk;
	auto status = get (transaction_a, table_a, key_a, junk);
	release_assert (status == MDB_SUCCESS || status == MDB_NOTFOUND);
	return (status == MDB_SUCCESS);
}

int nano::mdb_store::get (nano::transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a, nano::mdb_val & value_a) const
{
	return mdb_get (env.tx (transaction_a), table_to_dbi (table_a), key_a, value_a);
}

int nano::mdb_store::put (nano::write_transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a, const nano::mdb_val & value_a) const
{
	return (mdb_put (env.tx (transaction_a), table_to_dbi (table_a), key_a, value_a, 0));
}

int nano::mdb_store::del (nano::write_transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a) const
{
	return (mdb_del (env.tx (transaction_a), table_to_dbi (table_a), key_a, nullptr));
}

int nano::mdb_store::drop (nano::write_transaction const & transaction_a, tables table_a)
{
	return clear (transaction_a, table_to_dbi (table_a));
}

int nano::mdb_store::clear (nano::write_transaction const & transaction_a, MDB_dbi handle_a)
{
	return mdb_drop (env.tx (transaction_a), handle_a, 0);
}

size_t nano::mdb_store::count (nano::transaction const & transaction_a, tables table_a) const
{
	return count (transaction_a, table_to_dbi (table_a));
}

size_t nano::mdb_store::count (nano::transaction const & transaction_a, MDB_dbi db_a) const
{
	MDB_stat stats;
	auto status (mdb_stat (env.tx (transaction_a), db_a, &stats));
	release_assert (status == 0);
	return (stats.ms_entries);
}

MDB_dbi nano::mdb_store::table_to_dbi (tables table_a) const
{
	switch (table_a)
	{
		case tables::frontiers:
			return frontiers;
		case tables::accounts_v0:
			return accounts_v0;
		case tables::accounts_v1:
			return accounts_v1;
		case tables::send_blocks:
			return send_blocks;
		case tables::receive_blocks:
			return receive_blocks;
		case tables::open_blocks:
			return open_blocks;
		case tables::change_blocks:
			return change_blocks;
		case tables::state_blocks_v0:
			return state_blocks_v0;
		case tables::state_blocks_v1:
			return state_blocks_v1;
		case tables::pending_v0:
			return pending_v0;
		case tables::pending_v1:
			return pending_v1;
		case tables::blocks_info:
			return blocks_info;
		case tables::unchecked:
			return unchecked;
		case tables::vote:
			return vote;
		case tables::online_weight:
			return online_weight;
		case tables::meta:
			return meta;
		case tables::peers:
			return peers;
		case tables::confirmation_height:
			return confirmation_height;
		default:
			release_assert (false);
			return peers;
	}
}

bool nano::mdb_store::not_found (int status) const
{
	return (status_code_not_found () == status);
}

bool nano::mdb_store::success (int status) const
{
	return (MDB_SUCCESS == status);
}

int nano::mdb_store::status_code_not_found () const
{
	return MDB_NOTFOUND;
}

bool nano::mdb_store::copy_db (boost::filesystem::path const & destination_file)
{
	return !mdb_env_copy2 (env.environment, destination_file.string ().c_str (), MDB_CP_COMPACT);
}

bool nano::mdb_store::init_error () const
{
	return error;
}
