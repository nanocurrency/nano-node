#include <nano/node/dht/dht.hpp>
#include <nano/node/lmdb/lmdb.hpp>

#include <algorithm>
#include <memory>

#include <diskhash.hpp>

// TODO: keep this until diskhash builds fine on Windows
#ifndef _WIN32

namespace nano
{
nano::unchecked_dht_mdb_store::unchecked_dht_mdb_store (
nano::dht_mdb_store & dht_mdb_store_a,
const boost::filesystem::path & dht_path_a) :
	unchecked_mdb_store{ dht_mdb_store_a },
	dht_impl (
	std::make_unique<dht::DiskHash<unchecked_info_dht>> (
	(const char *)dht_path_a.c_str (),
	(const int)(sizeof_unchecked_key + 1), // hex code for each char + 1
	dht::DHOpenRW)),
	dht (*dht_impl)
{
}

template <>
void * unchecked_dht_val::data () const
{
	return (void *)value.mv_data;
}
template <>
size_t unchecked_dht_val::size () const
{
	return static_cast<unchecked_info_dht> (value).data_size ();
}
template <>
unchecked_dht_val::db_val (size_t size_a, void * data_a) :
	value{ data_a }
{
}
template <>
void unchecked_dht_val::convert_buffer_to_value ()
{
	release_assert (static_cast<unchecked_info_dht> (value).data_size () >= buffer->size ());
	value = { const_cast<uint8_t *> (buffer->data ()) };
}
}

#endif // _WIN32 -- TODO: keep this until diskhash builds fine on Windows
