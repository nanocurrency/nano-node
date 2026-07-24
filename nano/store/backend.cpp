#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>
#include <nano/store/backend.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <future>

namespace nano::store
{
nano::store::column_schema const backend::schema_meta{ { nano::store::table::meta, "meta" } };

backend::backend (nano::logger & logger, nano::store::txn_tracking_config const & txn_tracking_config)
{
	if (txn_tracking_config.enable)
	{
		tracker = std::make_unique<nano::store::txn_tracker> (logger, txn_tracking_config);
	}
}

backend::~backend () = default;

void backend::open (column_schema schema, nano::store::open_mode mode)
{
	if (is_open)
	{
		throw std::runtime_error ("Backend is already open: " + get_database_path ());
	}

	// Ensure schema is valid: no two tables may share the same name
	debug_assert (std::find_if (schema.begin (), schema.end (), [&schema] (auto const & definition) {
		return std::count_if (schema.begin (), schema.end (), [&definition] (auto const & other) { return other.name == definition.name; }) > 1;
	})
	== schema.end (),
	"distinct schema tables must not share a table name");

	open_impl (schema, mode);

	is_open = true;
	current_mode = mode;
	current_schema = schema;

	load_meta ();
	debug_assert (current_meta.has_value ());
}

void backend::create (column_schema schema, nano::store::backend_version_t version)
{
	if (is_open)
	{
		throw std::runtime_error ("Backend is already open: " + get_database_path ());
	}

	// Create and immediately close to initialize the database structure
	open (schema, nano::store::open_mode::read_write);

	// Ensure database doesn't already exist
	if (meta.version_exists (tx_begin_read ()))
	{
		throw std::runtime_error ("Attempting to create a database that already exists: " + get_database_path ());
	}

	// Set the version in the meta table
	meta.put_version (tx_begin_write (), version);

	close ();
}

void backend::close ()
{
	close_impl ();

	is_open = false;
	current_mode = {};
	current_meta.reset ();
	current_schema.clear ();
}

auto backend::fetch_meta () -> std::optional<backend_meta>
{
	// Attempt to open just the meta table to check if database exists
	try
	{
		open (schema_meta, nano::store::open_mode::read_only);
	}
	catch (nano::error const & error)
	{
		if (error == nano::error_backend::db_not_found)
		{
			return std::nullopt;
		}
		throw;
	}

	load_meta ();
	debug_assert (current_meta.has_value ());
	auto result = current_meta.value ();

	close ();

	return result;
}

void backend::load_meta ()
{
	backend_meta info{};
	info.version = meta.get_version (tx_begin_read ());
	current_meta = info;
}

auto backend::get_meta () const -> backend_meta
{
	release_assert (current_meta.has_value (), "meta information has not been loaded");
	return current_meta.value ();
}

auto backend::get_schema () const -> column_schema
{
	release_assert (is_open, "backend is not open");
	return current_schema;
}

auto backend::schema_definition (nano::store::table table) const -> column_definition const &
{
	release_assert (is_open, "backend is not open");
	auto it = std::find_if (current_schema.begin (), current_schema.end (), [table] (auto const & definition) {
		return definition.table == table;
	});
	release_assert (it != current_schema.end (), "table is not part of the open schema");
	return *it;
}

void backend::create_table (nano::store::table table)
{
	if (table_open (table))
	{
		return;
	}
	release_assert (current_mode != nano::store::open_mode::read_only, "tables cannot be created while the backend is opened in read-only mode");
	auto const & definition = schema_definition (table);
	release_assert (definition.kind == table_kind::optional, "only optional tables may be created on demand");
	create_table_impl (table, definition.name);
}

bool backend::drop_table (nano::store::table table)
{
	release_assert (current_mode != nano::store::open_mode::read_only, "tables cannot be dropped while the backend is opened in read-only mode");
	return drop_table_by_name (schema_definition (table).name);
}

auto backend::get_mode () const -> std::optional<nano::store::open_mode>
{
	return is_open ? std::optional{ current_mode } : std::nullopt;
}

auto backend::get_version (nano::store::transaction const & txn) const -> nano::store::backend_version_t
{
	return meta.get_version (txn);
}

void backend::set_version (nano::store::write_transaction const & txn, nano::store::backend_version_t version)
{
	meta.put_version (txn, version);
}

bool backend::empty (nano::store::transaction const & txn, nano::store::table table) const
{
	return begin (txn, table) == end (txn, table);
}

bool backend::empty (nano::store::transaction const & txn) const
{
	release_assert (is_open, "backend is not open");
	for (auto const & definition : get_schema ())
	{
		if (!table_open (definition.table))
		{
			continue; // Absent optional tables are empty
		}
		if (!empty (txn, definition.table))
		{
			return false;
		}
	}
	return true;
}

uint64_t backend::count_exact (nano::store::transaction const & txn, nano::store::table table) const
{
	uint64_t result = 0;
	for (auto i = begin (txn, table), n = end (txn, table); i != n; ++i)
	{
		++result;
	}
	return result;
}

void backend::for_each_par (nano::store::table table, std::function<void (read_transaction const &, iterator, iterator)> const & action) const
{
	// Split based on first byte of keys (0-255)
	// This works regardless of actual key type/length
	unsigned const thread_count = std::max (10u, std::min (40u, 10 * nano::hardware_concurrency ()));
	unsigned const split = 256 / thread_count;

	std::vector<std::future<void>> futures;
	futures.reserve (thread_count);

	for (unsigned i = 0; i < thread_count; ++i)
	{
		bool const is_last = (i == thread_count - 1);

		futures.emplace_back (std::async (std::launch::async, [this, table, &action, i, split, is_last] {
			nano::thread_role::set (nano::thread_role::name::db_parallel_traversal);

			// Create 32-byte key with first byte set to split boundary
			// Using 32 bytes ensures it works with 256-bit and 512-bit keys
			std::array<uint8_t, 32> start_bytes{};
			std::array<uint8_t, 32> end_bytes{};
			start_bytes[0] = static_cast<uint8_t> (i * split);
			end_bytes[0] = static_cast<uint8_t> ((i + 1) * split);

			auto txn = this->tx_begin_read ();
			nano::store::db_val start_key{ start_bytes };
			nano::store::db_val end_key{ end_bytes };

			action (txn,
			this->begin (txn, table, start_key),
			is_last ? this->end (txn, table) : this->begin (txn, table, end_key));
		}));
	}

	// Wait for all futures and rethrow any exceptions
	for (auto & future : futures)
	{
		future.get (); // Rethrows exception if one occurred
	}
}

void backend::copy_to (backend & destination, copy_progress_callback callback, size_t batch_size) const
{
	if (!destination.empty (destination.tx_begin_read ()))
	{
		throw std::runtime_error ("Destination backend is not empty: " + destination.get_database_path ());
	}

	auto const schema = get_schema ();
	size_t const total_tables = schema.size ();
	size_t table_index = 0;

	for (auto const & definition : schema)
	{
		auto const table = definition.table;
		auto const & table_name = definition.name;
		// Absent optional tables have nothing to copy; present ones must be created on the destination first
		if (!table_open (table))
		{
			++table_index;
			continue;
		}
		if (definition.kind == table_kind::optional && !destination.table_open (table))
		{
			destination.create_table (table);
		}
		auto src_txn = tx_begin_read ();
		uint64_t const total = count (src_txn, table);
		std::atomic<uint64_t> copied{ 0 };

		auto copy_action = [&] (nano::store::read_transaction const &, iterator begin_it, iterator end_it) {
			auto dst_txn = destination.tx_begin_write ();
			size_t batch_count = 0;

			for (auto it = std::move (begin_it); it != end_it; ++it)
			{
				auto const & [key, value] = *it;
				auto status = destination.put (dst_txn, table, nano::store::db_val{ key }, nano::store::db_val{ value });
				if (!destination.success (status))
				{
					throw std::runtime_error ("copy_to: put failed: " + destination.error_string (status));
				}

				auto current_copied = ++copied;
				++batch_count;

				if (batch_size > 0 && batch_count >= batch_size)
				{
					dst_txn.refresh ();
					batch_count = 0;
				}

				if (callback && (current_copied % 100000 == 0 || current_copied == total))
				{
					callback (copy_progress{ table_index, total_tables, table, table_name,
					current_copied, total });
				}
			}
		};

		// Use for_each_par for all tables
		// It splits by first byte of keys - works regardless of key type
		for_each_par (table, copy_action);

		++table_index;
	}
}

void backend::collect_txn_tracker (boost::property_tree::ptree & ptree, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time) const
{
	if (tracker)
	{
		tracker->serialize_json (ptree, min_read_time, min_write_time);
	}
}

auto backend::txn_tracking_callbacks () const -> nano::store::txn_callbacks
{
	txn_callbacks callbacks{};
	if (tracker)
	{
		callbacks.txn_start = [this] (nano::store::transaction_impl const * txn) {
			tracker->add (txn);
		};
		callbacks.txn_end = [this] (nano::store::transaction_impl const * txn) {
			tracker->erase (txn);
		};
	}
	return callbacks;
}

void backend::collect_memory_stats (boost::property_tree::ptree &) const
{
	// Default implementation does nothing - backend-specific
}
}

namespace nano
{
std::string error_backend_messages::message (int ev) const
{
	switch (static_cast<nano::error_backend> (ev))
	{
		case nano::error_backend::generic:
			return "Generic backend error";
		case nano::error_backend::db_not_found:
			return "Database not found";
		case nano::error_backend::table_not_found:
			return "Table not found";
		case nano::error_backend::failure:
			return "Backend operation failed";
	}
	return "Invalid error code";
}
}
