#include <nano/lib/files.hpp>
#include <nano/lib/logging.hpp>
#include <nano/store/backend.hpp>
#include <nano/store/db_val.hpp>
#include <nano/store/db_val_templ.hpp>
#include <nano/store/tables.hpp>
#include <nano/test_common/make_store.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <ranges>

namespace
{
// Test schema with meta (required) and accounts table for general testing
nano::store::column_schema const test_schema{
	{ nano::store::table::meta, "meta" },
	{ nano::store::table::accounts, "accounts" },
	{ nano::store::table::blocks, "blocks" },
};

// Helper to create a simple key from an integer
nano::uint256_union make_key (uint64_t value)
{
	nano::uint256_union key{};
	key.qwords[0] = value;
	return key;
}

// Helper to create a simple value from an integer
nano::uint256_union make_value (uint64_t value)
{
	nano::uint256_union val{};
	val.qwords[0] = value;
	return val;
}
}

/*
 * Basic operations
 */

TEST (backend, basic_put_get)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);
	auto value = make_value (42);

	auto write_tx = backend->tx_begin_write ();
	auto put_status = backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value });
	ASSERT_TRUE (backend->success (put_status));
	write_tx.commit ();

	auto read_tx = backend->tx_begin_read ();
	nano::store::db_val result;
	auto get_status = backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);
	ASSERT_TRUE (backend->success (get_status));
	ASSERT_EQ (result.size (), sizeof (nano::uint256_union));
	auto result_value = result.convert_to<nano::uint256_union> ();
	EXPECT_EQ (result_value, value);
}

TEST (backend, put_overwrite)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);
	auto value1 = make_value (100);
	auto value2 = make_value (200);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value1 });
	}
	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value2 });
	}

	auto read_tx = backend->tx_begin_read ();
	nano::store::db_val result;
	backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);
	auto result_value = result.convert_to<nano::uint256_union> ();
	EXPECT_EQ (result_value, value2);
}

TEST (backend, get_non_existent)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (999);

	auto read_tx = backend->tx_begin_read ();
	nano::store::db_val result;
	auto status = backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);
	EXPECT_TRUE (backend->not_found (status));
}

TEST (backend, exists_after_put)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);
	auto value = make_value (42);

	auto write_tx = backend->tx_begin_write ();
	EXPECT_FALSE (backend->exists (write_tx, nano::store::table::accounts, nano::store::db_val{ key }));
	backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value });
	EXPECT_TRUE (backend->exists (write_tx, nano::store::table::accounts, nano::store::db_val{ key }));
}

TEST (backend, delete_existing)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);
	auto value = make_value (42);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value });
	}
	{
		auto write_tx = backend->tx_begin_write ();
		EXPECT_TRUE (backend->exists (write_tx, nano::store::table::accounts, nano::store::db_val{ key }));
		auto status = backend->del (write_tx, nano::store::table::accounts, nano::store::db_val{ key });
		EXPECT_TRUE (backend->success (status));
		EXPECT_FALSE (backend->exists (write_tx, nano::store::table::accounts, nano::store::db_val{ key }));
	}
}

TEST (backend, delete_non_existent)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (999);

	auto write_tx = backend->tx_begin_write ();
	EXPECT_FALSE (backend->exists (write_tx, nano::store::table::accounts, nano::store::db_val{ key }));
	auto status = backend->del (write_tx, nano::store::table::accounts, nano::store::db_val{ key });
	// Backends should return success for delete of non-existent key
	EXPECT_TRUE (backend->success (status));
}

TEST (backend, exists_after_delete)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);
	auto value = make_value (42);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value });
	}
	{
		auto write_tx = backend->tx_begin_write ();
		backend->del (write_tx, nano::store::table::accounts, nano::store::db_val{ key });
	}

	auto read_tx = backend->tx_begin_read ();
	EXPECT_FALSE (backend->exists (read_tx, nano::store::table::accounts, nano::store::db_val{ key }));
}

TEST (backend, get_after_delete)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);
	auto value = make_value (42);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value });
	}
	{
		auto write_tx = backend->tx_begin_write ();
		backend->del (write_tx, nano::store::table::accounts, nano::store::db_val{ key });
	}

	auto read_tx = backend->tx_begin_read ();
	nano::store::db_val result;
	auto status = backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);
	EXPECT_TRUE (backend->not_found (status));
}

/*
 * Binary data edge cases
 */

TEST (backend, empty_value)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	// Test with nullptr
	{
		auto key = make_key (1);
		nano::store::db_val empty_value{ nullptr };

		auto write_tx = backend->tx_begin_write ();
		auto put_status = backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, empty_value);
		ASSERT_TRUE (backend->success (put_status));
		write_tx.commit ();

		auto read_tx = backend->tx_begin_read ();
		nano::store::db_val result;
		auto get_status = backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);
		ASSERT_TRUE (backend->success (get_status));
		EXPECT_EQ (result.size (), 0);
	}

	// Test with empty span
	{
		auto key = make_key (2);
		nano::store::db_val empty_value{ std::span<uint8_t const>{} };

		auto write_tx = backend->tx_begin_write ();
		auto put_status = backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, empty_value);
		ASSERT_TRUE (backend->success (put_status));
		write_tx.commit ();

		auto read_tx = backend->tx_begin_read ();
		nano::store::db_val result;
		auto get_status = backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);
		ASSERT_TRUE (backend->success (get_status));
		EXPECT_EQ (result.size (), 0);
	}
}

TEST (backend, binary_null_bytes)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	// Key with embedded null bytes
	std::vector<uint8_t> key_data = { 0x01, 0x00, 0x02, 0x00, 0x03 };
	std::vector<uint8_t> value_data = { 0x00, 0xFF, 0x00, 0xFF, 0x00 };

	nano::store::db_val key{ key_data };
	nano::store::db_val value{ value_data };

	auto write_tx = backend->tx_begin_write ();
	auto put_status = backend->put (write_tx, nano::store::table::accounts, key, value);
	ASSERT_TRUE (backend->success (put_status));
	write_tx.commit ();

	auto read_tx = backend->tx_begin_read ();
	nano::store::db_val result;
	auto get_status = backend->get (read_tx, nano::store::table::accounts, key, result);
	ASSERT_TRUE (backend->success (get_status));
	ASSERT_EQ (result.size (), value_data.size ());
	EXPECT_TRUE (std::ranges::equal (result.span_view, value_data));
}

TEST (backend, binary_all_bytes)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	// Value with all possible byte values (0x00-0xFF)
	std::vector<uint8_t> all_bytes (256);
	for (int i = 0; i < 256; ++i)
	{
		all_bytes[i] = static_cast<uint8_t> (i);
	}

	auto key = make_key (1);
	nano::store::db_val value{ all_bytes };

	auto write_tx = backend->tx_begin_write ();
	auto put_status = backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, value);
	ASSERT_TRUE (backend->success (put_status));
	write_tx.commit ();

	auto read_tx = backend->tx_begin_read ();
	nano::store::db_val result;
	auto get_status = backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);
	ASSERT_TRUE (backend->success (get_status));
	ASSERT_EQ (result.size (), all_bytes.size ());
	EXPECT_TRUE (std::ranges::equal (result.span_view, all_bytes));
}

TEST (backend, large_value)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	// Large value (64KB)
	std::vector<uint8_t> large_data (64 * 1024);
	for (size_t i = 0; i < large_data.size (); ++i)
	{
		large_data[i] = static_cast<uint8_t> (i % 256);
	}

	auto key = make_key (1);
	nano::store::db_val value{ large_data };

	auto write_tx = backend->tx_begin_write ();
	auto put_status = backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, value);
	ASSERT_TRUE (backend->success (put_status));
	write_tx.commit ();

	auto read_tx = backend->tx_begin_read ();
	nano::store::db_val result;
	auto get_status = backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);
	ASSERT_TRUE (backend->success (get_status));
	ASSERT_EQ (result.size (), large_data.size ());
	EXPECT_TRUE (std::ranges::equal (result.span_view, large_data));
}

// Keys should be ordered lexicographically
TEST (backend, probe_key)
{
	// Test that 32-byte probe keys work with longer actual keys (like 64-byte/512-bit)
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	// Insert 64-byte keys with different first bytes
	{
		auto write_tx = backend->tx_begin_write ();
		for (uint8_t first_byte : { 0x20, 0x60, 0xA0, 0xE0 })
		{
			std::array<uint8_t, 64> key{};
			key[0] = first_byte;
			key[63] = 0x99; // Some data at the end
			backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ make_value (first_byte) });
		}
	}

	auto read_tx = backend->tx_begin_read ();

	// Probe with 32-byte key (shorter than actual 64-byte keys)
	{
		std::array<uint8_t, 32> probe{};
		probe[0] = 0x50; // Between 0x20 and 0x60

		auto it = backend->begin (read_tx, nano::store::table::accounts, nano::store::db_val{ probe });
		ASSERT_FALSE (it.is_end ());
		auto [k, v] = *it;
		EXPECT_EQ (k.size (), 64);
		EXPECT_EQ (k[0], 0x60); // Should find the 64-byte key starting with 0x60
	}

	// Probe with 32-byte key at boundary
	{
		std::array<uint8_t, 32> probe{};
		probe[0] = 0xA0;

		auto it = backend->begin (read_tx, nano::store::table::accounts, nano::store::db_val{ probe });
		ASSERT_FALSE (it.is_end ());
		auto [k, v] = *it;
		EXPECT_EQ (k[0], 0xA0);
	}
}

/*
 * Iterators
 */

TEST (backend, iterator_empty_table)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto read_tx = backend->tx_begin_read ();
	auto begin_it = backend->begin (read_tx, nano::store::table::accounts);
	auto end_it = backend->end (read_tx, nano::store::table::accounts);

	EXPECT_TRUE (begin_it.is_end ());
	EXPECT_EQ (begin_it, end_it);
}

TEST (backend, iterator_single_entry)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);
	auto value = make_value (42);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value });
	}

	auto read_tx = backend->tx_begin_read ();
	auto it = backend->begin (read_tx, nano::store::table::accounts);
	auto end_it = backend->end (read_tx, nano::store::table::accounts);

	ASSERT_FALSE (it.is_end ());
	ASSERT_NE (it, end_it);

	auto [k, v] = *it;
	EXPECT_EQ (k.size (), sizeof (nano::uint256_union));

	++it;
	EXPECT_TRUE (it.is_end ());
}

TEST (backend, iterator_forward)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	// Insert keys in non-sorted order to verify ordering
	std::vector<nano::uint256_union> keys;
	for (uint64_t i : { 3, 1, 2 })
	{
		keys.push_back (make_key (i));
	}

	{
		auto write_tx = backend->tx_begin_write ();
		for (auto const & key : keys)
		{
			backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ make_value (0) });
		}
	}

	// Verify lexicographic order (key1 < key2 < key3)
	auto read_tx = backend->tx_begin_read ();
	auto it = backend->begin (read_tx, nano::store::table::accounts);
	auto end_it = backend->end (read_tx, nano::store::table::accounts);

	std::vector<nano::uint256_union> found_keys;
	for (; it != end_it; ++it)
	{
		auto [k, v] = *it;
		auto found_key = nano::store::db_val{ k }.convert_to<nano::uint256_union> ();
		found_keys.push_back (found_key);
	}

	ASSERT_EQ (found_keys.size (), 3);
	EXPECT_EQ (found_keys[0], make_key (1));
	EXPECT_EQ (found_keys[1], make_key (2));
	EXPECT_EQ (found_keys[2], make_key (3));
}

TEST (backend, iterator_decrement_from_end)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key1 = make_key (1);
	auto key2 = make_key (2);
	auto key3 = make_key (3);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key1 }, nano::store::db_val{ make_value (0) });
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key2 }, nano::store::db_val{ make_value (0) });
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key3 }, nano::store::db_val{ make_value (0) });
	}

	auto read_tx = backend->tx_begin_read ();
	auto end_it = backend->end (read_tx, nano::store::table::accounts);

	// Circular behavior: decrement end() should give last key (key3)
	--end_it;
	ASSERT_FALSE (end_it.is_end ());
	auto [k, v] = *end_it;
	auto found_key = nano::store::db_val{ k }.convert_to<nano::uint256_union> ();
	EXPECT_EQ (found_key, key3);
}

TEST (backend, iterator_increment_from_end)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key1 = make_key (1);
	auto key2 = make_key (2);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key1 }, nano::store::db_val{ make_value (0) });
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key2 }, nano::store::db_val{ make_value (0) });
	}

	auto read_tx = backend->tx_begin_read ();
	auto end_it = backend->end (read_tx, nano::store::table::accounts);

	// Circular behavior: increment end() should give first key (key1)
	++end_it;
	ASSERT_FALSE (end_it.is_end ());
	auto [k, v] = *end_it;
	auto found_key = nano::store::db_val{ k }.convert_to<nano::uint256_union> ();
	EXPECT_EQ (found_key, key1);
}

TEST (backend, iterator_wrap_forward)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ make_value (0) });
	}

	auto read_tx = backend->tx_begin_read ();
	auto it = backend->begin (read_tx, nano::store::table::accounts);

	// After incrementing past the last element, should reach end
	++it;
	EXPECT_TRUE (it.is_end ());

	// Incrementing from end should wrap to first element
	++it;
	ASSERT_FALSE (it.is_end ());
	auto [k, v] = *it;
	auto found_key = nano::store::db_val{ k }.convert_to<nano::uint256_union> ();
	EXPECT_EQ (found_key, key);
}

TEST (backend, iterator_wrap_backward)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ make_value (0) });
	}

	auto read_tx = backend->tx_begin_read ();
	auto it = backend->begin (read_tx, nano::store::table::accounts);

	// Decrement from first element should wrap to end
	--it;
	EXPECT_TRUE (it.is_end ());
}

TEST (backend, iterator_lower_bound_exact)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key1 = make_key (1);
	auto key2 = make_key (2);
	auto key3 = make_key (3);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key1 }, nano::store::db_val{ make_value (0) });
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key2 }, nano::store::db_val{ make_value (0) });
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key3 }, nano::store::db_val{ make_value (0) });
	}

	auto read_tx = backend->tx_begin_read ();
	auto it = backend->begin (read_tx, nano::store::table::accounts, nano::store::db_val{ key2 });

	ASSERT_FALSE (it.is_end ());
	auto [k, v] = *it;
	auto found_key = nano::store::db_val{ k }.convert_to<nano::uint256_union> ();
	EXPECT_EQ (found_key, key2);
}

TEST (backend, iterator_lower_bound_between)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key1 = make_key (1);
	auto key3 = make_key (3);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key1 }, nano::store::db_val{ make_value (0) });
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key3 }, nano::store::db_val{ make_value (0) });
	}

	auto read_tx = backend->tx_begin_read ();
	auto key2 = make_key (2); // Key between key1 and key3
	auto it = backend->begin (read_tx, nano::store::table::accounts, nano::store::db_val{ key2 });

	// Should find key3 (next key >= key2)
	ASSERT_FALSE (it.is_end ());
	auto [k, v] = *it;
	auto found_key = nano::store::db_val{ k }.convert_to<nano::uint256_union> ();
	EXPECT_EQ (found_key, key3);
}

TEST (backend, iterator_lower_bound_past_all)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key1 = make_key (1);
	auto key2 = make_key (2);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key1 }, nano::store::db_val{ make_value (0) });
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key2 }, nano::store::db_val{ make_value (0) });
	}

	auto read_tx = backend->tx_begin_read ();
	auto large_key = make_key (999); // Key greater than all existing keys
	auto it = backend->begin (read_tx, nano::store::table::accounts, nano::store::db_val{ large_key });

	// Should return end() since no key >= large_key
	EXPECT_TRUE (it.is_end ());
}

TEST (backend, iterator_reverse_traversal)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key1 = make_key (1);
	auto key2 = make_key (2);
	auto key3 = make_key (3);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key1 }, nano::store::db_val{ make_value (0) });
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key2 }, nano::store::db_val{ make_value (0) });
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key3 }, nano::store::db_val{ make_value (0) });
	}

	auto read_tx = backend->tx_begin_read ();
	auto end_it = backend->end (read_tx, nano::store::table::accounts);

	// Traverse backwards from end
	std::vector<nano::uint256_union> found_keys;
	--end_it;
	while (!end_it.is_end ())
	{
		auto [k, v] = *end_it;
		auto found_key = nano::store::db_val{ k }.convert_to<nano::uint256_union> ();
		found_keys.push_back (found_key);
		--end_it;
	}

	ASSERT_EQ (found_keys.size (), 3);
	EXPECT_EQ (found_keys[0], key3); // Last key first
	EXPECT_EQ (found_keys[1], key2);
	EXPECT_EQ (found_keys[2], key1); // First key last
}

TEST (backend, iterator_is_end)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ make_value (0) });
	}

	auto read_tx = backend->tx_begin_read ();

	auto begin_it = backend->begin (read_tx, nano::store::table::accounts);
	EXPECT_FALSE (begin_it.is_end ());

	auto end_it = backend->end (read_tx, nano::store::table::accounts);
	EXPECT_TRUE (end_it.is_end ());
}

/*
 * Transactions
 */

TEST (backend, tx_read_basic)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);
	auto value = make_value (42);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value });
		write_tx.commit ();
	}

	auto read_tx = backend->tx_begin_read ();
	nano::store::db_val result;
	auto status = backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);
	EXPECT_TRUE (backend->success (status));
}

TEST (backend, tx_write_commit)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);
	auto value = make_value (42);

	auto write_tx = backend->tx_begin_write ();
	backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value });
	write_tx.commit ();

	// Verify data persisted after explicit commit
	auto read_tx = backend->tx_begin_read ();
	EXPECT_TRUE (backend->exists (read_tx, nano::store::table::accounts, nano::store::db_val{ key }));
}

TEST (backend, tx_write_auto_commit)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);
	auto value = make_value (42);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value });
		// No explicit commit - destructor should commit
	}

	// Verify data persisted after auto-commit
	auto read_tx = backend->tx_begin_read ();
	EXPECT_TRUE (backend->exists (read_tx, nano::store::table::accounts, nano::store::db_val{ key }));
}

TEST (backend, tx_refresh)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);
	auto value1 = make_value (100);
	auto value2 = make_value (200);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value1 });
		write_tx.commit ();
	}

	// Start read transaction
	auto read_tx = backend->tx_begin_read ();
	nano::store::db_val result;
	backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);
	auto result_value = result.convert_to<nano::uint256_union> ();
	EXPECT_EQ (result_value, value1);

	// Update value in separate write transaction
	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value2 });
		write_tx.commit ();
	}

	// After refresh, read_tx should see new value
	read_tx.refresh ();
	backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);
	result_value = result.convert_to<nano::uint256_union> ();
	EXPECT_EQ (result_value, value2);
}

TEST (backend, tx_epoch_increments)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto read_tx = backend->tx_begin_read ();
	auto initial_epoch = read_tx.epoch ();

	read_tx.refresh ();
	auto new_epoch = read_tx.epoch ();

	EXPECT_GT (new_epoch, initial_epoch);
}

TEST (backend, tx_multiple_read)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);
	auto value = make_value (42);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value });
		write_tx.commit ();
	}

	// Multiple concurrent read transactions should work
	auto read_tx1 = backend->tx_begin_read ();
	auto read_tx2 = backend->tx_begin_read ();

	EXPECT_TRUE (backend->exists (read_tx1, nano::store::table::accounts, nano::store::db_val{ key }));
	EXPECT_TRUE (backend->exists (read_tx2, nano::store::table::accounts, nano::store::db_val{ key }));
}

TEST (backend, tx_read_sees_committed)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (1);
	auto value = make_value (42);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value });
		write_tx.commit ();
	}

	// Read transaction started after commit should see data
	auto read_tx = backend->tx_begin_read ();
	EXPECT_TRUE (backend->exists (read_tx, nano::store::table::accounts, nano::store::db_val{ key }));
}

TEST (backend, tx_read_isolation)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key1 = make_key (1);
	auto key2 = make_key (2);
	auto key3 = make_key (3);
	auto value1 = make_value (100);
	auto value2 = make_value (200);

	// Initial write - add key1
	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key1 }, nano::store::db_val{ value1 });
	}

	// Start read transaction (snapshot) - should see key1=value1, no key2, no key3
	auto read_tx = backend->tx_begin_read ();

	// Concurrent writes: update key1, add key2, add key3
	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key1 }, nano::store::db_val{ value2 });
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key2 }, nano::store::db_val{ value2 });
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key3 }, nano::store::db_val{ value2 });
	}

	// Test get() isolation - should see old value for key1
	{
		nano::store::db_val result;
		auto status = backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key1 }, result);
		ASSERT_TRUE (backend->success (status));
		auto result_value = result.convert_to<nano::uint256_union> ();
		EXPECT_EQ (result_value, value1);
	}

	// Test exists() isolation - should not see key2 that was added after snapshot
	{
		EXPECT_TRUE (backend->exists (read_tx, nano::store::table::accounts, nano::store::db_val{ key1 }));
		EXPECT_FALSE (backend->exists (read_tx, nano::store::table::accounts, nano::store::db_val{ key2 }));
	}

	// Test iterator isolation - should only see key1, not key2 or key3
	{
		auto it = backend->begin (read_tx, nano::store::table::accounts);
		auto end = backend->end (read_tx, nano::store::table::accounts);

		std::vector<nano::uint256_union> found_keys;
		for (; it != end; ++it)
		{
			auto [k, v] = *it;
			auto found_key = nano::store::db_val{ k }.convert_to<nano::uint256_union> ();
			found_keys.push_back (found_key);
		}

		ASSERT_EQ (found_keys.size (), 1);
		EXPECT_EQ (found_keys[0], key1);
	}

	// Test iterator with lower_bound isolation
	{
		auto it = backend->begin (read_tx, nano::store::table::accounts, nano::store::db_val{ key2 });
		// key2 doesn't exist in snapshot, and key3 also doesn't exist, so should be end
		EXPECT_TRUE (it.is_end ());
	}
}

TEST (backend, tx_write_read_isolation)
{
	// Skip test for LMDB as it does not support concurrent write transactions
	if (nano::default_database_backend () == nano::database_backend::lmdb)
	{
		GTEST_SKIP ();
	}

	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key1 = make_key (1);
	auto key2 = make_key (2);
	auto key3 = make_key (3);
	auto value1 = make_value (100);
	auto value2 = make_value (200);

	// Initial write - add key1
	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key1 }, nano::store::db_val{ value1 });
	}

	// Start write transaction - snapshot taken here
	auto write_tx = backend->tx_begin_write ();

	// Another write transaction commits changes AFTER our write_tx started
	{
		auto other_write_tx = backend->tx_begin_write ();
		backend->put (other_write_tx, nano::store::table::accounts, nano::store::db_val{ key1 }, nano::store::db_val{ value2 });
		backend->put (other_write_tx, nano::store::table::accounts, nano::store::db_val{ key2 }, nano::store::db_val{ value2 });
		backend->put (other_write_tx, nano::store::table::accounts, nano::store::db_val{ key3 }, nano::store::db_val{ value2 });
	}

	// Test get() isolation within write transaction - should see old value for key1
	{
		nano::store::db_val result;
		auto status = backend->get (write_tx, nano::store::table::accounts, nano::store::db_val{ key1 }, result);
		ASSERT_TRUE (backend->success (status));
		auto result_value = result.convert_to<nano::uint256_union> ();
		EXPECT_EQ (result_value, value1);
	}

	// Test exists() isolation within write transaction
	{
		EXPECT_TRUE (backend->exists (write_tx, nano::store::table::accounts, nano::store::db_val{ key1 }));
		EXPECT_FALSE (backend->exists (write_tx, nano::store::table::accounts, nano::store::db_val{ key2 }));
	}

	// Test iterator isolation within write transaction
	{
		auto it = backend->begin (write_tx, nano::store::table::accounts);
		auto end = backend->end (write_tx, nano::store::table::accounts);

		std::vector<nano::uint256_union> found_keys;
		for (; it != end; ++it)
		{
			auto [k, v] = *it;
			auto found_key = nano::store::db_val{ k }.convert_to<nano::uint256_union> ();
			found_keys.push_back (found_key);
		}

		ASSERT_EQ (found_keys.size (), 1);
		EXPECT_EQ (found_keys[0], key1);
	}
}

/*
 * Table operations
 */

TEST (backend, count_empty)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto read_tx = backend->tx_begin_read ();
	EXPECT_EQ (backend->count (read_tx, nano::store::table::accounts), 0);
}

TEST (backend, count_accuracy)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	{
		auto write_tx = backend->tx_begin_write ();
		for (uint64_t i = 0; i < 10; ++i)
		{
			backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ make_key (i) }, nano::store::db_val{ make_value (i) });
		}
	}

	auto read_tx = backend->tx_begin_read ();
	EXPECT_EQ (backend->count (read_tx, nano::store::table::accounts), 10);

	// Delete some entries
	{
		auto write_tx = backend->tx_begin_write ();
		backend->del (write_tx, nano::store::table::accounts, nano::store::db_val{ make_key (0) });
		backend->del (write_tx, nano::store::table::accounts, nano::store::db_val{ make_key (1) });
	}

	auto read_tx2 = backend->tx_begin_read ();
	EXPECT_EQ (backend->count (read_tx2, nano::store::table::accounts), 8);
}

TEST (backend, empty_true_false)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	{
		auto read_tx = backend->tx_begin_read ();
		EXPECT_TRUE (backend->empty (read_tx, nano::store::table::accounts));
	}
	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ make_key (1) }, nano::store::db_val{ make_value (1) });
	}
	{
		auto read_tx = backend->tx_begin_read ();
		EXPECT_FALSE (backend->empty (read_tx, nano::store::table::accounts));
	}
}

TEST (backend, empty_vs_count)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	// empty() should be reliable even when count() might return estimates
	{
		auto read_tx = backend->tx_begin_read ();
		EXPECT_TRUE (backend->empty (read_tx, nano::store::table::accounts));
		// When empty, count should be 0 or close to 0
		auto count = backend->count (read_tx, nano::store::table::accounts);
		EXPECT_EQ (count, 0);
	}

	// Add entries
	{
		auto write_tx = backend->tx_begin_write ();
		for (uint64_t i = 0; i < 100; ++i)
		{
			backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ make_key (i) }, nano::store::db_val{ make_value (i) });
		}
	}

	{
		auto read_tx = backend->tx_begin_read ();
		EXPECT_FALSE (backend->empty (read_tx, nano::store::table::accounts));
	}
}

TEST (backend, clear_table)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	{
		auto write_tx = backend->tx_begin_write ();
		for (uint64_t i = 0; i < 10; ++i)
		{
			backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ make_key (i) }, nano::store::db_val{ make_value (i) });
		}
	}
	{
		auto status = backend->clear (nano::store::table::accounts);
		EXPECT_TRUE (backend->success (status));
	}

	auto read_tx = backend->tx_begin_read ();
	EXPECT_TRUE (backend->empty (read_tx, nano::store::table::accounts));
}

TEST (backend, clear_empty_table)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	// Clear on empty table should succeed
	auto status = backend->clear (nano::store::table::accounts);
	EXPECT_TRUE (backend->success (status));
}

TEST (backend, table_exists)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	EXPECT_TRUE (backend->table_exists ("accounts"));
	EXPECT_TRUE (backend->table_exists ("meta"));
	EXPECT_FALSE (backend->table_exists ("nonexistent_table"));
}

TEST (backend, drop_table)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ make_key (1) }, nano::store::db_val{ make_value (1) });
	}

	EXPECT_TRUE (backend->table_exists ("accounts"));

	EXPECT_TRUE (backend->drop_table ("accounts"));

	EXPECT_FALSE (backend->table_exists ("accounts"));

	// Drop non-existent table should return false
	EXPECT_FALSE (backend->drop_table ("nonexistent_table"));
}

// Test dropping a table that exists in the database but not in the current schema
// This simulates database upgrades where old tables need to be removed
TEST (backend, drop_table_not_in_schema)
{
	auto path = nano::unique_path ();

	// Schema with an extra table (simulating old version)
	nano::store::column_schema const old_schema{
		{ nano::store::table::meta, "meta" },
		{ nano::store::table::accounts, "accounts" },
		{ nano::store::table::frontiers, "frontiers" }, // Dropped in v24
	};

	// Create database with old schema
	auto backend = nano::test::make_backend (path);
	{
		backend->create (old_schema, 1);
		backend->open (old_schema, nano::store::open_mode::read_write);

		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::frontiers, nano::store::db_val{ make_key (1) }, nano::store::db_val{ make_value (1) });
	}
	backend->close ();

	// Reopen with new schema (without frontiers) and drop the old table
	backend->open (test_schema, nano::store::open_mode::read_write);
	EXPECT_TRUE (backend->table_exists ("frontiers"));
	EXPECT_TRUE (backend->drop_table ("frontiers"));
	EXPECT_FALSE (backend->table_exists ("frontiers"));
}

/*
 * Lifecycle
 */

TEST (backend, open_create_close)
{
	auto path = nano::unique_path ();
	{
		auto backend = nano::test::make_backend (path);
		backend->create (test_schema, 1);
		backend->open (test_schema, nano::store::open_mode::read_write);

		auto key = make_key (1);
		auto value = make_value (42);

		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value });
		write_tx.commit ();

		backend->close ();
	}
}

TEST (backend, reopen_persistence)
{
	auto path = nano::unique_path ();
	auto key = make_key (1);
	auto value = make_value (42);

	// Write data
	{
		auto backend = nano::test::make_backend (path);
		backend->create (test_schema, 1);
		backend->open (test_schema, nano::store::open_mode::read_write);

		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ key }, nano::store::db_val{ value });
		write_tx.commit ();

		backend->close ();
	}

	// Reopen and verify
	{
		auto backend = nano::test::make_backend (path);
		backend->open (test_schema, nano::store::open_mode::read_write);

		auto read_tx = backend->tx_begin_read ();
		EXPECT_TRUE (backend->exists (read_tx, nano::store::table::accounts, nano::store::db_val{ key }));

		nano::store::db_val result;
		backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);
		auto result_value = result.convert_to<nano::uint256_union> ();
		EXPECT_EQ (result_value, value);
	}
}

TEST (backend, get_database_path)
{
	auto path = nano::unique_path ();
	auto backend = nano::test::make_backend (path);
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto db_path = backend->get_database_path ();
	// Path should contain the unique path we specified
	EXPECT_FALSE (db_path.empty ());
	EXPECT_NE (db_path.find (path.string ()), std::string::npos);
}

/*
 * Versioning
 */

TEST (backend, set_get_version)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->set_version (write_tx, 42);
		write_tx.commit ();
	}

	auto read_tx = backend->tx_begin_read ();
	EXPECT_EQ (backend->get_version (read_tx), 42);
}

/*
 * Status codes
 */

TEST (backend, success_not_found)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (999);

	auto read_tx = backend->tx_begin_read ();
	nano::store::db_val result;
	auto status = backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);

	EXPECT_FALSE (backend->success (status));
	EXPECT_TRUE (backend->not_found (status));
}

TEST (backend, error_string)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	auto key = make_key (999);

	auto read_tx = backend->tx_begin_read ();
	nano::store::db_val result;
	auto status = backend->get (read_tx, nano::store::table::accounts, nano::store::db_val{ key }, result);

	auto error_msg = backend->error_string (status);
	EXPECT_FALSE (error_msg.empty ());
}

/*
 * Exception tests
 */

TEST (backend, open_already_open_throws)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	// Attempting to open an already open backend should throw
	EXPECT_THROW (backend->open (test_schema, nano::store::open_mode::read_write), std::runtime_error);
}

TEST (backend, create_already_open_throws)
{
	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	// Attempting to create when backend is already open should throw
	EXPECT_THROW (backend->create (test_schema, 2), std::runtime_error);
}

TEST (backend, create_existing_database_throws)
{
	auto path = nano::unique_path ();

	// First create the database
	{
		auto backend = nano::test::make_backend (path);
		backend->create (test_schema, 1);
	}

	// Attempting to create again should throw
	{
		auto backend = nano::test::make_backend (path);
		EXPECT_THROW (backend->create (test_schema, 2), std::runtime_error);
	}
}

TEST (backend, open_nonexistent_readonly_throws)
{
	auto path = nano::unique_path ();
	auto backend = nano::test::make_backend (path);

	// Opening a non-existent database in read-only mode should throw nano::error with db_not_found
	EXPECT_THROW (
	{
		try
		{
			backend->open (test_schema, nano::store::open_mode::read_only);
		}
		catch (nano::error const & e)
		{
			EXPECT_EQ (e, nano::error_backend::db_not_found);
			throw;
		}
	},
	nano::error);
}

TEST (backend, fetch_meta_nonexistent_returns_nullopt)
{
	auto path = nano::unique_path ();
	auto backend = nano::test::make_backend (path);

	// fetch_meta on non-existent database should return nullopt, not throw
	auto meta = backend->fetch_meta ();
	EXPECT_FALSE (meta.has_value ());
}

TEST (backend, copy_to_nonempty_destination_throws)
{
	auto src_path = nano::unique_path ();
	auto dst_path = nano::unique_path ();

	auto src_backend = nano::test::make_backend (src_path);
	src_backend->create (test_schema, 1);
	src_backend->open (test_schema, nano::store::open_mode::read_write);

	auto dst_backend = nano::test::make_backend (dst_path);
	dst_backend->create (test_schema, 1);
	dst_backend->open (test_schema, nano::store::open_mode::read_write);

	// Add data to destination backend
	{
		auto write_tx = dst_backend->tx_begin_write ();
		dst_backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ make_key (1) }, nano::store::db_val{ make_value (1) });
	}

	// Copying to non-empty destination should throw
	EXPECT_THROW (src_backend->copy_to (*dst_backend), std::runtime_error);
}

// Death tests for iterator-transaction epoch validation
// The naming convention `_DeathTest` tells gtest to use threadsafe death test style

TEST (backend_DeathTest, iterator_epoch_check_read_refresh)
{
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	{
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ make_key (1) }, nano::store::db_val{ make_value (1) });
	}

	// Refreshing a read transaction while an iterator is alive should trigger assertion on iterator destruction
	ASSERT_DEATH ({
		auto read_tx = backend->tx_begin_read ();
		auto it = backend->begin (read_tx, nano::store::table::accounts);
		read_tx.refresh ();
		// Iterator destructor fires here with mismatched epoch
	},
	"invalid iterator-transaction lifetime detected");
}

TEST (backend_DeathTest, iterator_epoch_check_write_refresh)
{
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	auto backend = nano::test::make_backend ();
	backend->create (test_schema, 1);
	backend->open (test_schema, nano::store::open_mode::read_write);

	// Refreshing a write transaction while an iterator is alive should trigger assertion on iterator destruction
	ASSERT_DEATH ({
		auto write_tx = backend->tx_begin_write ();
		backend->put (write_tx, nano::store::table::accounts, nano::store::db_val{ make_key (1) }, nano::store::db_val{ make_value (1) });
		auto it = backend->begin (write_tx, nano::store::table::accounts);
		write_tx.refresh ();
		// Iterator destructor fires here with mismatched epoch
	},
	"invalid iterator-transaction lifetime detected");
}