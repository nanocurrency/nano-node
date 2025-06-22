#pragma once

#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/memory.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/fwd.hpp>
#include <nano/store/fwd.hpp>
#include <nano/store/tables.hpp>
#include <nano/store/transaction.hpp>
#include <nano/store/versioning.hpp>
#include <nano/store/write_queue.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>
#include <boost/property_tree/ptree.hpp>

#include <stack>

namespace nano
{
namespace store
{
	enum class open_mode
	{
		read_only,
		read_write
	};

	std::string_view to_string (open_mode mode);

	/**
	 * Store manager
	 */
	class component
	{
		friend class rocksdb_block_store_tombstone_count_Test;
		friend class mdb_block_store_upgrade_v21_v22_Test;

	public:
		explicit component (
		nano::store::block &,
		nano::store::account &,
		nano::store::pending &,
		nano::store::online_weight &,
		nano::store::pruned &,
		nano::store::peer &,
		nano::store::confirmation_height &,
		nano::store::final_vote &,
		nano::store::version &,
		nano::store::rep_weight &);

		virtual ~component () = default;

		void initialize (store::write_transaction const &, nano::ledger_constants &);

		virtual uint64_t count (store::transaction const & transaction_a, tables table_a) const = 0;
		virtual int drop (store::write_transaction const & transaction_a, tables table_a) = 0;
		virtual bool not_found (int status) const = 0;
		virtual bool success (int status) const = 0;
		virtual std::string error_string (int status) const = 0;

		store::block & block;
		store::account & account;
		store::pending & pending;
		store::rep_weight & rep_weight;

		static int constexpr version_minimum{ 21 };
		static int constexpr version_current{ 24 };

	public:
		store::online_weight & online_weight;
		store::pruned & pruned;
		store::peer & peer;
		store::confirmation_height & confirmation_height;
		store::final_vote & final_vote;
		store::version & version;

	public: // TODO: Shouldn't be public
		store::write_queue write_queue;

	public:
		virtual unsigned max_block_write_batch_num () const = 0;

		virtual bool copy_db (std::filesystem::path const & destination) = 0;
		virtual void rebuild_db (write_transaction const & transaction_a) = 0;

		/** Not applicable to all sub-classes */
		virtual void serialize_mdb_tracker (::boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds){};
		virtual void serialize_memory_stats (::boost::property_tree::ptree &) = 0;

		virtual bool init_error () const = 0;

		/** Start read-write transaction */
		virtual write_transaction tx_begin_write () = 0;

		/** Start read-only transaction */
		virtual read_transaction tx_begin_read () const = 0;

		virtual std::string vendor_get () const = 0;
		virtual std::filesystem::path get_database_path () const = 0;
		virtual nano::store::open_mode get_mode () const = 0;
	};
} // namespace store
} // namespace nano
