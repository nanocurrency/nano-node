#include <nano/store/lmdb/db_val.hpp>

namespace nano
{
template <>
void * mdb_val::data () const
{
	return value.mv_data;
}

template <>
std::size_t mdb_val::size () const
{
	return value.mv_size;
}

template <>
mdb_val::db_val (std::size_t size_a, void * data_a) :
	value ({ size_a, data_a })
{
}

template <>
void mdb_val::convert_buffer_to_value ()
{
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}
}
