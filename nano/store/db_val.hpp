#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/lib/stream.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/store/block.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/versioning.hpp>

#include <cstddef>

namespace celerix
{
class account_info;
class account_info_v22;
class block;
class pending_info;
class pending_key;
class vote;
}

namespace celerix::store
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

	db_val (celerix::uint128_union const & val_a) :
		db_val (sizeof (val_a), const_cast<celerix::uint128_union *> (&val_a))
	{
	}

	db_val (celerix::uint256_union const & val_a) :
		db_val (sizeof (val_a), const_cast<celerix::uint256_union *> (&val_a))
	{
	}

	db_val (celerix::uint512_union const & val_a) :
		db_val (sizeof (val_a), const_cast<celerix::uint512_union *> (&val_a))
	{
	}

	db_val (celerix::qualified_root const & val_a) :
		db_val (sizeof (val_a), const_cast<celerix::qualified_root *> (&val_a))
	{
	}

	db_val (celerix::account_info const & val_a);

	db_val (celerix::account_info_v22 const & val_a);

	db_val (celerix::pending_info const & val_a);

	db_val (celerix::pending_key const & val_a);

	db_val (celerix::confirmation_height_info const & val_a) :
		buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			celerix::vectorstream stream (*buffer);
			val_a.serialize (stream);
		}
		convert_buffer_to_value ();
	}

	db_val (celerix::block_info const & val_a) :
		db_val (sizeof (val_a), const_cast<celerix::block_info *> (&val_a))
	{
		static_assert (std::is_standard_layout<celerix::block_info>::value, "Standard layout is required");
	}

	db_val (celerix::endpoint_key const & val_a) :
		db_val (sizeof (val_a), const_cast<celerix::endpoint_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<celerix::endpoint_key>::value, "Standard layout is required");
	}

	db_val (std::shared_ptr<celerix::block> const & val_a);

	db_val (uint64_t val_a) :
		buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			boost::endian::native_to_big_inplace (val_a);
			celerix::vectorstream stream (*buffer);
			celerix::write (stream, val_a);
		}
		convert_buffer_to_value ();
	}

	explicit operator celerix::account_info () const;
	explicit operator celerix::account_info_v22 () const;

	explicit operator block_info () const
	{
		celerix::block_info result;
		debug_assert (size () == sizeof (result));
		static_assert (sizeof (celerix::block_info::account) + sizeof (celerix::block_info::balance) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator celerix::pending_info () const;

	explicit operator celerix::pending_key () const;

	explicit operator celerix::confirmation_height_info () const
	{
		celerix::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		celerix::confirmation_height_info result;
		bool error (result.deserialize (stream));
		(void)error;
		debug_assert (!error);
		return result;
	}

	explicit operator celerix::uint128_union () const
	{
		return convert<celerix::uint128_union> ();
	}

	explicit operator celerix::amount () const
	{
		return convert<celerix::amount> ();
	}

	explicit operator celerix::block_hash () const
	{
		return convert<celerix::block_hash> ();
	}

	explicit operator celerix::public_key () const
	{
		return convert<celerix::public_key> ();
	}

	explicit operator celerix::qualified_root () const
	{
		return convert<celerix::qualified_root> ();
	}

	explicit operator celerix::uint256_union () const
	{
		return convert<celerix::uint256_union> ();
	}

	explicit operator celerix::uint512_union () const
	{
		return convert<celerix::uint512_union> ();
	}

	explicit operator std::array<char, 64> () const
	{
		celerix::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		std::array<char, 64> result;
		auto error = celerix::try_read (stream, result);
		(void)error;
		debug_assert (!error);
		return result;
	}

	explicit operator celerix::endpoint_key () const
	{
		celerix::endpoint_key result;
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator block_w_sideband () const;

	explicit operator std::nullptr_t () const
	{
		return nullptr;
	}

	explicit operator celerix::no_value () const
	{
		return no_value::dummy;
	}

	explicit operator std::shared_ptr<celerix::block> () const;

	template <typename Block>
	std::shared_ptr<Block> convert_to_block () const
	{
		celerix::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (std::make_shared<Block> (error, stream));
		debug_assert (!error);
		return result;
	}

	explicit operator std::shared_ptr<celerix::send_block> () const
	{
		return convert_to_block<celerix::send_block> ();
	}

	explicit operator std::shared_ptr<celerix::receive_block> () const
	{
		return convert_to_block<celerix::receive_block> ();
	}

	explicit operator std::shared_ptr<celerix::open_block> () const
	{
		return convert_to_block<celerix::open_block> ();
	}

	explicit operator std::shared_ptr<celerix::change_block> () const
	{
		return convert_to_block<celerix::change_block> ();
	}

	explicit operator std::shared_ptr<celerix::state_block> () const
	{
		return convert_to_block<celerix::state_block> ();
	}

	explicit operator std::shared_ptr<celerix::vote> () const
	{
		celerix::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (celerix::make_shared<celerix::vote> (error, stream));
		debug_assert (!error);
		return result;
	}

	explicit operator uint64_t () const
	{
		uint64_t result;
		celerix::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (celerix::try_read (stream, result));
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
} // namespace celerix::store
