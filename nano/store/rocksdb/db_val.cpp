#include <nano/store/rocksdb/db_val.hpp>

template <>
void * nano::store::rocksdb::db_val::data () const
{
	return (void *)value.data ();
}

template <>
std::size_t nano::store::rocksdb::db_val::size () const
{
	return value.size ();
}

template <>
nano::store::rocksdb::db_val::db_val (std::size_t size_a, void * data_a) :
	value (static_cast<char const *> (data_a), size_a)
{
}

template <>
void nano::store::rocksdb::db_val::convert_buffer_to_value ()
{
	value = ::rocksdb::Slice (reinterpret_cast<char const *> (buffer->data ()), buffer->size ());
}
