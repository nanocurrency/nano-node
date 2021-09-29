// TODO: keep this until diskhash builds fine on Windows
#ifndef _WIN32

#include <nano/node/dht/dht.hpp>
#include <nano/node/lmdb/lmdb.hpp>

#include <algorithm>
#include <memory>

#include <diskhash.hpp>

namespace nano
{
unchecked_dht_mdb_store::unchecked_dht_mdb_store (
nano::dht_mdb_store & dht_mdb_store_a,
const boost::filesystem::path & dht_path_a) :
	unchecked_mdb_store{ dht_mdb_store_a },
	local_dht_mdb_store (dht_mdb_store_a),
	dht_impl (
	std::make_unique<dht::DiskHash<DHT_unchecked_info>> (
	(const char *)dht_path_a.c_str (),
	(const int)(sizeof_unchecked_key + 1), // hex code for each char + 1
	dht::DHOpenRW)),
	dht (*dht_impl)
{
}

void unchecked_dht_mdb_store::clear (nano::write_transaction const &)
{
	dht.clear ();
}

nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_dht_mdb_store::end () const
{
	return nano::store_iterator<nano::unchecked_key, nano::unchecked_info> (nullptr);
}

nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_dht_mdb_store::begin (nano::transaction const & transaction_a) const
{
	return local_dht_mdb_store.make_iterator<nano::unchecked_key, nano::unchecked_info> (transaction_a, tables::unchecked, true);
}

nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_dht_mdb_store::begin (nano::transaction const & transaction_a, nano::unchecked_key const & key_a) const
{
	return local_dht_mdb_store.make_iterator<nano::unchecked_key, nano::unchecked_info> (transaction_a, tables::unchecked, nano::db_val<DHT_unchecked_info> (key_a));
}

template <>
void * unchecked_info_dht_val::data () const
{
	return (void *)value.data;
}
template <>
size_t unchecked_info_dht_val::size () const
{
	return static_cast<DHT_unchecked_info> (value).size;
}
template <>
unchecked_info_dht_val::db_val (size_t size_a, void * data_a) :
	value{ data_a, size_a }
{
}
template <>
void unchecked_info_dht_val::convert_buffer_to_value ()
{
	release_assert (static_cast<DHT_unchecked_info> (value).max_size () >= buffer->size ());
	value = { const_cast<uint8_t *> (buffer->data ()), buffer->size () };
}
}

#endif // _WIN32 -- TODO: keep this until diskhash builds fine on Windows
