#include <nano/store/rocksdb/db_val.hpp>

namespace nano
{
template <>
void * rocksdb_val::data () const
{
	return (void *)value.data ();
}

template <>
std::size_t rocksdb_val::size () const
{
	return value.size ();
}

template <>
rocksdb_val::db_val (std::size_t size_a, void * data_a) :
	value (static_cast<char const *> (data_a), size_a)
{
}

template <>
void rocksdb_val::convert_buffer_to_value ()
{
	value = ::rocksdb::Slice (reinterpret_cast<char const *> (buffer->data ()), buffer->size ());
}
}
