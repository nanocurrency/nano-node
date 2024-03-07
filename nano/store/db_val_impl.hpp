#include <nano/lib/blocks.hpp>
#include <nano/store/db_val.hpp>

template <typename T>
nano::store::db_val<T>::db_val (std::shared_ptr<nano::block> const & val_a) :
	buffer (std::make_shared<std::vector<uint8_t>> ())
{
	{
		nano::vectorstream stream (*buffer);
		nano::serialize_block (stream, *val_a);
	}
	convert_buffer_to_value ();
}

template <typename T>
nano::store::db_val<T>::operator std::shared_ptr<nano::block> () const
{
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
	std::shared_ptr<nano::block> result (nano::deserialize_block (stream));
	return result;
}

template <typename T>
nano::store::db_val<T>::operator nano::store::block_w_sideband () const
{
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
	nano::store::block_w_sideband block_w_sideband;
	block_w_sideband.block = (nano::deserialize_block (stream));
	auto error = block_w_sideband.sideband.deserialize (stream, block_w_sideband.block->type ());
	release_assert (!error);
	block_w_sideband.block->sideband_set (block_w_sideband.sideband);
	return block_w_sideband;
}
