#pragma once

#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/memory.hpp>
#include <nano/secure/buffer.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/tables.hpp>
#include <nano/store/transaction.hpp>
#include <nano/store/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

#include <stack>

namespace nano
{
class account_store;
class block_store;
class confirmation_height_store;
class final_vote_store;
class frontier_store;
class ledger_cache;
class online_weight_store;
class peer_store;
class pending_store;
class pruned_store;
class version_store;

namespace store
{
	/**
 * Store manager
 */
	class component
	{
		friend class rocksdb_block_store_tombstone_count_Test;
		friend class mdb_block_store_upgrade_v21_v22_Test;

	public:
		// clang-format off
	explicit component (
		nano::block_store &,
		nano::frontier_store &,
		nano::account_store &,
		nano::pending_store &,
		nano::online_weight_store &,
		nano::pruned_store &,
		nano::peer_store &,
		nano::confirmation_height_store &,
		nano::final_vote_store &,
		nano::version_store &
	);
		// clang-format on
		virtual ~component () = default;
		void initialize (nano::write_transaction const & transaction_a, nano::ledger_cache & ledger_cache_a, nano::ledger_constants & constants);
		virtual uint64_t count (nano::transaction const & transaction_a, tables table_a) const = 0;
		virtual int drop (nano::write_transaction const & transaction_a, tables table_a) = 0;
		virtual bool not_found (int status) const = 0;
		virtual bool success (int status) const = 0;
		virtual int status_code_not_found () const = 0;
		virtual std::string error_string (int status) const = 0;

		block_store & block;
		frontier_store & frontier;
		account_store & account;
		pending_store & pending;
		static int constexpr version_minimum{ 14 };
		static int constexpr version_current{ 22 };

	public:
		online_weight_store & online_weight;
		pruned_store & pruned;
		peer_store & peer;
		confirmation_height_store & confirmation_height;
		final_vote_store & final_vote;
		version_store & version;

		virtual unsigned max_block_write_batch_num () const = 0;

		virtual bool copy_db (boost::filesystem::path const & destination) = 0;
		virtual void rebuild_db (nano::write_transaction const & transaction_a) = 0;

		/** Not applicable to all sub-classes */
		virtual void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds){};
		virtual void serialize_memory_stats (boost::property_tree::ptree &) = 0;

		virtual bool init_error () const = 0;

		/** Start read-write transaction */
		virtual nano::write_transaction tx_begin_write (std::vector<nano::tables> const & tables_to_lock = {}, std::vector<nano::tables> const & tables_no_lock = {}) = 0;

		/** Start read-only transaction */
		virtual nano::read_transaction tx_begin_read () const = 0;

		virtual std::string vendor_get () const = 0;
	};
} // namespace store
} // namespace nano
