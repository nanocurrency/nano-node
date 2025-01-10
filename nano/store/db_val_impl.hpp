#pragma once

#include <celerix/lib/blocks.hpp>
#include <celerix/secure/account_info.hpp>
#include <celerix/secure/pending_info.hpp>
#include <celerix/store/db_val.hpp>

template <typename T>
celerix::store::db_val<T>::db_val (celerix::account_info const & val_a) :
	db_val (val_a.db_size (), const_cast<celerix::account_info *> (&val_a))
{
}

template <typename T>
celerix::store::db_val<T>::db_val (celerix::account_info_v22 const & val_a) :
	db_val (val_a.db_size (), const_cast<celerix::account_info_v22 *> (&val_a))
{
}

template <typename T>
celerix::store::db_val<T>::db_val (std::shared_ptr<celerix::block> const & val_a) :
	buffer (std::make_shared<std::vector<uint8_t>> ())
{
	{
		celerix::vectorstream stream (*buffer);
		celerix::serialize_block (stream, *val_a);
	}
	convert_buffer_to_value ();
}

template <typename T>
celerix::store::db_val<T>::db_val (celerix::pending_info const & val_a) :
	db_val (val_a.db_size (), const_cast<celerix::pending_info *> (&val_a))
{
	static_assert (std::is_standard_layout<celerix::pending_info>::value, "Standard layout is required");
}

template <typename T>
celerix::store::db_val<T>::db_val (celerix::pending_key const & val_a) :
	db_val (sizeof (val_a), const_cast<celerix::pending_key *> (&val_a))
{
	static_assert (std::is_standard_layout<celerix::pending_key>::value, "Standard layout is required");
}

template <typename T>
celerix::store::db_val<T>::operator celerix::account_info () const
{
	celerix::account_info result;
	debug_assert (size () == result.db_size ());
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

template <typename T>
celerix::store::db_val<T>::operator celerix::account_info_v22 () const
{
	celerix::account_info_v22 result;
	debug_assert (size () == result.db_size ());
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

template <typename T>
celerix::store::db_val<T>::operator std::shared_ptr<celerix::block> () const
{
	celerix::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
	std::shared_ptr<celerix::block> result (celerix::deserialize_block (stream));
	return result;
}

template <typename T>
celerix::store::db_val<T>::operator celerix::store::block_w_sideband () const
{
	celerix::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
	celerix::store::block_w_sideband block_w_sideband;
	block_w_sideband.block = (celerix::deserialize_block (stream));
	auto error = block_w_sideband.sideband.deserialize (stream, block_w_sideband.block->type ());
	release_assert (!error);
	block_w_sideband.block->sideband_set (block_w_sideband.sideband);
	return block_w_sideband;
}

template <typename T>
celerix::store::db_val<T>::operator celerix::pending_info () const
{
	celerix::pending_info result;
	debug_assert (size () == result.db_size ());
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

template <typename T>
celerix::store::db_val<T>::operator celerix::pending_key () const
{
	celerix::pending_key result;
	debug_assert (size () == sizeof (result));
	static_assert (sizeof (celerix::pending_key::account) + sizeof (celerix::pending_key::hash) == sizeof (result), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}
