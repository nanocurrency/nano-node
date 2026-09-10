#pragma once

#include <nano/store/backend.hpp>
#include <nano/store/ledger_store.hpp>

#include <functional>
#include <memory>

namespace nano::test
{
/*
 * Store backend that wraps another backend and runs a callback right before every point access, so a test can observe or interleave at exactly that moment.
 * Everything else is forwarded unchanged, opening and table creation included, so a ledger store can be built on it like on the wrapped backend.
 */
class hooked_backend final : public nano::store::backend
{
public:
	using hook = std::function<void (nano::store::table)>;

	hooked_backend (std::unique_ptr<nano::store::backend> inner, nano::logger &);

	// Runs on the calling thread before get, put, del and exists reach the wrapped backend
	hook before_access;

public: // nano::store::backend
	int get (nano::store::transaction const &, nano::store::table, nano::store::db_val const & key, nano::store::db_val & value) const override;
	int put (nano::store::write_transaction const &, nano::store::table, nano::store::db_val const & key, nano::store::db_val const & value) override;
	int del (nano::store::write_transaction const &, nano::store::table, nano::store::db_val const & key) override;
	bool exists (nano::store::transaction const &, nano::store::table, nano::store::db_val const & key) const override;
	uint64_t count (nano::store::transaction const &, nano::store::table) const override;
	bool count_is_exact (nano::store::table) const override;
	int clear (nano::store::table) override;
	bool table_open (nano::store::table) const override;
	bool drop_table_by_name (std::string const & name) override;
	bool table_exists (std::string const & name) const override;
	nano::store::iterator begin (nano::store::transaction const &, nano::store::table) const override;
	nano::store::iterator begin (nano::store::transaction const &, nano::store::table, nano::store::db_val const & key) const override;
	nano::store::iterator end (nano::store::transaction const &, nano::store::table) const override;
	void copy_with_compaction (std::filesystem::path const & destination) override;
	void backup () override;
	void collect_txn_tracker (boost::property_tree::ptree &, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time) const override;
	void collect_memory_stats (boost::property_tree::ptree &) const override;
	bool success (int status) const override;
	bool not_found (int status) const override;
	std::string error_string (int status) const override;
	nano::store::read_transaction tx_begin_read () const override;
	nano::store::write_transaction tx_begin_write () override;
	std::string get_vendor () const override;
	std::string get_database_path () const override;

protected:
	void open_impl (nano::store::column_schema, nano::store::open_mode) override;
	void close_impl () override;
	void create_table_impl (nano::store::table, std::string const & name) override;

private:
	void notify (nano::store::table) const;

	std::unique_ptr<nano::store::backend> inner;
};

// A ledger store built on a hooked backend, with the backend exposed so the test can install its hook
struct hooked_store
{
	std::unique_ptr<nano::store::ledger_store> store;
	hooked_backend & backend;
};

// Fresh ledger store on the default database backend, wrapped in a hooked_backend
hooked_store make_hooked_store ();
}
