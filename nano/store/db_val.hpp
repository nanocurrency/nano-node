#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/stream.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/block.hpp>
#include <nano/store/component.hpp>
#include <nano/store/versioning.hpp>

#include <cstddef>

namespace nano
{
class account_info;
class account_info_v22;
class block;
class pending_info;
class pending_key;
}

namespace nano::store
{
/**
 * Encapsulates database specific container
 */
template <typename Val>
class db_val
{
public:
	db_val (Val const & value_a) :
		value (value_a)
	{
	}

	db_val () :
		db_val (0, nullptr)
	{
	}

	db_val (std::nullptr_t) :
		db_val (0, this)
	{
	}

	db_val (nano::uint128_union const & val_a) :
		db_val (sizeof (val_a), const_cast<nano::uint128_union *> (&val_a))
	{
	}

	db_val (nano::uint256_union const & val_a) :
		db_val (sizeof (val_a), const_cast<nano::uint256_union *> (&val_a))
	{
	}

	db_val (nano::uint512_union const & val_a) :
		db_val (sizeof (val_a), const_cast<nano::uint512_union *> (&val_a))
	{
	}

	db_val (nano::qualified_root const & val_a) :
		db_val (sizeof (val_a), const_cast<nano::qualified_root *> (&val_a))
	{
	}

	db_val (nano::account_info const & val_a);

	db_val (nano::account_info_v22 const & val_a);

	db_val (nano::pending_info const & val_a);

	db_val (nano::pending_key const & val_a);

	db_val (nano::confirmation_height_info const & val_a) :
		buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			nano::vectorstream stream (*buffer);
			val_a.serialize (stream);
		}
		convert_buffer_to_value ();
	}

	db_val (nano::block_info const & val_a) :
		db_val (sizeof (val_a), const_cast<nano::block_info *> (&val_a))
	{
		static_assert (std::is_standard_layout<nano::block_info>::value, "Standard layout is required");
	}

	db_val (nano::endpoint_key const & val_a) :
		db_val (sizeof (val_a), const_cast<nano::endpoint_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<nano::endpoint_key>::value, "Standard layout is required");
	}

	db_val (std::shared_ptr<nano::block> const & val_a);

	db_val (uint64_t val_a) :
		buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			boost::endian::native_to_big_inplace (val_a);
			nano::vectorstream stream (*buffer);
			nano::write (stream, val_a);
		}
		convert_buffer_to_value ();
	}

	explicit operator nano::account_info () const;
	explicit operator nano::account_info_v22 () const;

	explicit operator block_info () const
	{
		nano::block_info result;
		debug_assert (size () == sizeof (result));
		static_assert (sizeof (nano::block_info::account) + sizeof (nano::block_info::balance) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator nano::pending_info () const;

	explicit operator nano::pending_key () const;

	explicit operator nano::confirmation_height_info () const
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		nano::confirmation_height_info result;
		bool error (result.deserialize (stream));
		(void)error;
		debug_assert (!error);
		return result;
	}

	explicit operator nano::uint128_union () const
	{
		return convert<nano::uint128_union> ();
	}

	explicit operator nano::amount () const
	{
		return convert<nano::amount> ();
	}

	explicit operator nano::block_hash () const
	{
		return convert<nano::block_hash> ();
	}

	explicit operator nano::public_key () const
	{
		return convert<nano::public_key> ();
	}

	explicit operator nano::qualified_root () const
	{
		return convert<nano::qualified_root> ();
	}

	explicit operator nano::uint256_union () const
	{
		return convert<nano::uint256_union> ();
	}

	explicit operator nano::uint512_union () const
	{
		return convert<nano::uint512_union> ();
	}

	explicit operator std::array<char, 64> () const
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		std::array<char, 64> result;
		auto error = nano::try_read (stream, result);
		(void)error;
		debug_assert (!error);
		return result;
	}

	explicit operator nano::endpoint_key () const
	{
		nano::endpoint_key result;
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator block_w_sideband () const;

	explicit operator std::nullptr_t () const
	{
		return nullptr;
	}

	explicit operator nano::no_value () const
	{
		return no_value::dummy;
	}

	explicit operator std::shared_ptr<nano::block> () const;

	template <typename Block>
	std::shared_ptr<Block> convert_to_block () const
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (std::make_shared<Block> (error, stream));
		debug_assert (!error);
		return result;
	}

	explicit operator std::shared_ptr<nano::send_block> () const
	{
		return convert_to_block<nano::send_block> ();
	}

	explicit operator std::shared_ptr<nano::receive_block> () const
	{
		return convert_to_block<nano::receive_block> ();
	}

	explicit operator std::shared_ptr<nano::open_block> () const
	{
		return convert_to_block<nano::open_block> ();
	}

	explicit operator std::shared_ptr<nano::change_block> () const
	{
		return convert_to_block<nano::change_block> ();
	}

	explicit operator std::shared_ptr<nano::state_block> () const
	{
		return convert_to_block<nano::state_block> ();
	}

	explicit operator std::shared_ptr<nano::vote> () const
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (nano::make_shared<nano::vote> (error, stream));
		debug_assert (!error);
		return result;
	}

	explicit operator uint64_t () const
	{
		uint64_t result;
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (nano::try_read (stream, result));
		(void)error;
		debug_assert (!error);
		boost::endian::big_to_native_inplace (result);
		return result;
	}

	operator Val * () const
	{
		// Allow passing a temporary to a non-c++ function which doesn't have constness
		return const_cast<Val *> (&value);
	}

	operator Val const & () const
	{
		return value;
	}

	// Must be specialized
	void * data () const;
	size_t size () const;
	db_val (size_t size_a, void * data_a);
	void convert_buffer_to_value ();

	Val value;
	std::shared_ptr<std::vector<uint8_t>> buffer;

private:
	template <typename T>
	T convert () const
	{
		T result;
		debug_assert (size () == sizeof (result));
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
		return result;
	}
};
} // namespace nano::store
