#include <nano/test_common/common.hpp>
#include <nano/test_common/hooked_backend.hpp>
#include <nano/test_common/make_store.hpp>

nano::test::hooked_backend::hooked_backend (std::unique_ptr<nano::store::backend> inner_a, nano::logger & logger) :
	nano::store::backend{ logger, nano::store::txn_tracking_config{} },
	inner{ std::move (inner_a) }
{
}

void nano::test::hooked_backend::notify (nano::store::table table) const
{
	if (before_access)
	{
		before_access (table);
	}
}

int nano::test::hooked_backend::get (nano::store::transaction const & txn, nano::store::table table, nano::store::db_val const & key, nano::store::db_val & value) const
{
	notify (table);
	return inner->get (txn, table, key, value);
}

int nano::test::hooked_backend::put (nano::store::write_transaction const & txn, nano::store::table table, nano::store::db_val const & key, nano::store::db_val const & value)
{
	notify (table);
	return inner->put (txn, table, key, value);
}

int nano::test::hooked_backend::del (nano::store::write_transaction const & txn, nano::store::table table, nano::store::db_val const & key)
{
	notify (table);
	return inner->del (txn, table, key);
}

bool nano::test::hooked_backend::exists (nano::store::transaction const & txn, nano::store::table table, nano::store::db_val const & key) const
{
	notify (table);
	return inner->exists (txn, table, key);
}

uint64_t nano::test::hooked_backend::count (nano::store::transaction const & txn, nano::store::table table) const
{
	return inner->count (txn, table);
}

bool nano::test::hooked_backend::count_is_exact (nano::store::table table) const
{
	return inner->count_is_exact (table);
}

int nano::test::hooked_backend::clear (nano::store::table table)
{
	return inner->clear (table);
}

bool nano::test::hooked_backend::table_open (nano::store::table table) const
{
	return inner->table_open (table);
}

bool nano::test::hooked_backend::drop_table_by_name (std::string const & name)
{
	return inner->drop_table_by_name (name);
}

bool nano::test::hooked_backend::table_exists (std::string const & name) const
{
	return inner->table_exists (name);
}

nano::store::iterator nano::test::hooked_backend::begin (nano::store::transaction const & txn, nano::store::table table) const
{
	return inner->begin (txn, table);
}

nano::store::iterator nano::test::hooked_backend::begin (nano::store::transaction const & txn, nano::store::table table, nano::store::db_val const & key) const
{
	return inner->begin (txn, table, key);
}

nano::store::iterator nano::test::hooked_backend::end (nano::store::transaction const & txn, nano::store::table table) const
{
	return inner->end (txn, table);
}

void nano::test::hooked_backend::copy_with_compaction (std::filesystem::path const & destination)
{
	inner->copy_with_compaction (destination);
}

void nano::test::hooked_backend::backup ()
{
	inner->backup ();
}

void nano::test::hooked_backend::collect_txn_tracker (boost::property_tree::ptree & tree, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time) const
{
	inner->collect_txn_tracker (tree, min_read_time, min_write_time);
}

void nano::test::hooked_backend::collect_memory_stats (boost::property_tree::ptree & tree) const
{
	inner->collect_memory_stats (tree);
}

bool nano::test::hooked_backend::success (int status) const
{
	return inner->success (status);
}

bool nano::test::hooked_backend::not_found (int status) const
{
	return inner->not_found (status);
}

std::string nano::test::hooked_backend::error_string (int status) const
{
	return inner->error_string (status);
}

nano::store::read_transaction nano::test::hooked_backend::tx_begin_read () const
{
	return inner->tx_begin_read ();
}

nano::store::write_transaction nano::test::hooked_backend::tx_begin_write ()
{
	return inner->tx_begin_write ();
}

std::string nano::test::hooked_backend::get_vendor () const
{
	return inner->get_vendor ();
}

std::string nano::test::hooked_backend::get_database_path () const
{
	return inner->get_database_path ();
}

// The wrapped backend keeps its own open state, so the lifecycle goes through its public entry points
void nano::test::hooked_backend::open_impl (nano::store::column_schema schema, nano::store::open_mode mode)
{
	inner->open (schema, mode);
}

void nano::test::hooked_backend::close_impl ()
{
	inner->close ();
}

void nano::test::hooked_backend::create_table_impl (nano::store::table table, std::string const &)
{
	inner->create_table (table);
}

nano::test::hooked_store nano::test::make_hooked_store ()
{
	auto backend = std::make_unique<nano::test::hooked_backend> (nano::test::make_backend (), nano::test::default_logger ());
	auto & backend_ref = *backend;
	auto store = std::make_unique<nano::store::ledger_store> (std::move (backend), nano::store::open_mode::read_write, nano::test::default_stats (), nano::test::default_logger ());
	return { std::move (store), backend_ref };
}
