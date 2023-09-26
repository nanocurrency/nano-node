#include <nano/store/lmdb/db_val.hpp>

template <>
void * nano::store::lmdb::db_val::data () const
{
	return value.mv_data;
}

template <>
std::size_t nano::store::lmdb::db_val::size () const
{
	return value.mv_size;
}

template <>
nano::store::lmdb::db_val::db_val (std::size_t size_a, void * data_a) :
	value ({ size_a, data_a })
{
}

template <>
void nano::store::lmdb::db_val::convert_buffer_to_value ()
{
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}
