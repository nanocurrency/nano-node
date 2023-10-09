#pragma once

#include <nano/lib/id_dispenser.hpp>
#include <nano/lib/lmdbconfig.hpp>
#include <nano/store/component.hpp>
#include <nano/store/lmdb/transaction_impl.hpp>

namespace
{
nano::id_dispenser id_gen;
}

namespace nano::store::lmdb
{
/**
 * RAII wrapper for MDB_env
 */
class env final
{
public:
	/** Environment options, most of which originates from the config file. */
	class options final
	{
		friend class env;

	public:
		static options make ()
		{
			return options ();
		}

		options & set_config (nano::lmdb_config config_a)
		{
			config = config_a;
			return *this;
		}

		options & set_use_no_mem_init (int use_no_mem_init_a)
		{
			use_no_mem_init = use_no_mem_init_a;
			return *this;
		}

		/** Used by the wallet to override the config map size */
		options & override_config_map_size (std::size_t map_size_a)
		{
			config.map_size = map_size_a;
			return *this;
		}

		/** Used by the wallet to override the sync strategy */
		options & override_config_sync (nano::lmdb_config::sync_strategy sync_a)
		{
			config.sync = sync_a;
			return *this;
		}

	private:
		bool use_no_mem_init{ false };
		nano::lmdb_config config;
	};

	env (bool &, boost::filesystem::path const &, env::options options_a = env::options::make ());
	void init (bool &, boost::filesystem::path const &, env::options options_a = env::options::make ());
	~env ();
	operator MDB_env * () const;
	store::read_transaction tx_begin_read (txn_callbacks callbacks = txn_callbacks{}) const;
	store::write_transaction tx_begin_write (txn_callbacks callbacks = txn_callbacks{}) const;
	MDB_txn * tx (store::transaction const & transaction_a) const;
	MDB_env * environment;
	nano::id_dispenser::id_t const store_id{ id_gen.next_id () };
};
} // namespace nano::store::lmdb
