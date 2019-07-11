#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/common.hpp>
#include <nano/node/lmdb.hpp>
#include <nano/secure/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

#include <queue>

#include <valgrind/valgrind.h>

nano::mdb_env::mdb_env (bool & error_a, boost::filesystem::path const & path_a, int max_dbs_a, bool use_no_mem_init_a, size_t map_size_a)
{
	boost::system::error_code error_mkdir, error_chmod;
	if (path_a.has_parent_path ())
	{
		boost::filesystem::create_directories (path_a.parent_path (), error_mkdir);
		nano::set_secure_perm_directory (path_a.parent_path (), error_chmod);
		if (!error_mkdir)
		{
			auto status1 (mdb_env_create (&environment));
			release_assert (status1 == 0);
			auto status2 (mdb_env_set_maxdbs (environment, max_dbs_a));
			release_assert (status2 == 0);
			auto running_within_valgrind = (RUNNING_ON_VALGRIND > 0);
			auto map_size = map_size_a;
			auto max_valgrind_map_size = 16 * 1024 * 1024;
			if (running_within_valgrind && map_size_a > max_valgrind_map_size)
			{
				// In order to run LMDB under Valgrind, the maximum map size must be smaller than half your available RAM
				map_size = max_valgrind_map_size;
			}
			auto status3 (mdb_env_set_mapsize (environment, map_size));
			release_assert (status3 == 0);
			// It seems if there's ever more threads than mdb_env_set_maxreaders has read slots available, we get failures on transaction creation unless MDB_NOTLS is specified
			// This can happen if something like 256 io_threads are specified in the node config
			// MDB_NORDAHEAD will allow platforms that support it to load the DB in memory as needed.
			// MDB_NOMEMINIT prevents zeroing malloc'ed pages. Can provide improvement for non-sensitive data but may make memory checkers noisy (e.g valgrind).
			auto environment_flags = MDB_NOSUBDIR | MDB_NOTLS | MDB_NORDAHEAD;
			if (!running_within_valgrind && use_no_mem_init_a)
			{
				environment_flags |= MDB_NOMEMINIT;
			}
			auto status4 (mdb_env_open (environment, path_a.string ().c_str (), environment_flags, 00600));
			if (status4 != 0)
			{
				std::cerr << "Could not open lmdb environment: " << status4;
				char * error_str (mdb_strerror (status4));
				if (error_str)
				{
					std::cerr << ", " << error_str;
				}
				std::cerr << std::endl;
			}
			release_assert (status4 == 0);
			error_a = status4 != 0;
		}
		else
		{
			error_a = true;
			environment = nullptr;
		}
	}
	else
	{
		error_a = true;
		environment = nullptr;
	}
}

nano::mdb_env::~mdb_env ()
{
	if (environment != nullptr)
	{
		mdb_env_close (environment);
	}
}

nano::mdb_env::operator MDB_env * () const
{
	return environment;
}

nano::read_transaction nano::mdb_env::tx_begin_read (mdb_txn_callbacks mdb_txn_callbacks) const
{
	return nano::read_transaction{ std::make_unique<nano::read_mdb_txn> (*this, mdb_txn_callbacks) };
}

nano::write_transaction nano::mdb_env::tx_begin_write (mdb_txn_callbacks mdb_txn_callbacks) const
{
	return nano::write_transaction{ std::make_unique<nano::write_mdb_txn> (*this, mdb_txn_callbacks) };
}

MDB_txn * nano::mdb_env::tx (nano::transaction const & transaction_a) const
{
	return static_cast<MDB_txn *> (transaction_a.get_handle ());
}

nano::read_mdb_txn::read_mdb_txn (nano::mdb_env const & environment_a, nano::mdb_txn_callbacks txn_callbacks_a) :
txn_callbacks (txn_callbacks_a)
{
	auto status (mdb_txn_begin (environment_a, nullptr, MDB_RDONLY, &handle));
	release_assert (status == 0);
	txn_callbacks.txn_start (this);
}

nano::read_mdb_txn::~read_mdb_txn ()
{
	// This uses commit rather than abort, as it is needed when opening databases with a read only transaction
	auto status (mdb_txn_commit (handle));
	release_assert (status == MDB_SUCCESS);
	txn_callbacks.txn_end (this);
}

void nano::read_mdb_txn::reset () const
{
	mdb_txn_reset (handle);
	txn_callbacks.txn_end (this);
}

void nano::read_mdb_txn::renew () const
{
	auto status (mdb_txn_renew (handle));
	release_assert (status == 0);
	txn_callbacks.txn_start (this);
}

void * nano::read_mdb_txn::get_handle () const
{
	return handle;
}

nano::write_mdb_txn::write_mdb_txn (nano::mdb_env const & environment_a, nano::mdb_txn_callbacks txn_callbacks_a) :
env (environment_a),
txn_callbacks (txn_callbacks_a)
{
	renew ();
}

nano::write_mdb_txn::~write_mdb_txn ()
{
	commit ();
}

void nano::write_mdb_txn::commit () const
{
	auto status (mdb_txn_commit (handle));
	release_assert (status == MDB_SUCCESS);
	txn_callbacks.txn_end (this);
}

void nano::write_mdb_txn::renew ()
{
	auto status (mdb_txn_begin (env, nullptr, 0, &handle));
	release_assert (status == MDB_SUCCESS);
	txn_callbacks.txn_start (this);
}

void * nano::write_mdb_txn::get_handle () const
{
	return handle;
}

nano::mdb_val::mdb_val (nano::epoch epoch_a) :
value ({ 0, nullptr }),
epoch (epoch_a)
{
}

nano::mdb_val::mdb_val (MDB_val const & value_a, nano::epoch epoch_a) :
value (value_a),
epoch (epoch_a)
{
}

nano::mdb_val::mdb_val (size_t size_a, void * data_a) :
value ({ size_a, data_a })
{
}

nano::mdb_val::mdb_val (nano::uint128_union const & val_a) :
mdb_val (sizeof (val_a), const_cast<nano::uint128_union *> (&val_a))
{
}

nano::mdb_val::mdb_val (nano::uint256_union const & val_a) :
mdb_val (sizeof (val_a), const_cast<nano::uint256_union *> (&val_a))
{
}

nano::mdb_val::mdb_val (nano::account_info const & val_a) :
mdb_val (val_a.db_size (), const_cast<nano::account_info *> (&val_a))
{
}

nano::mdb_val::mdb_val (nano::account_info_v13 const & val_a) :
mdb_val (val_a.db_size (), const_cast<nano::account_info_v13 *> (&val_a))
{
}

nano::mdb_val::mdb_val (nano::pending_info const & val_a) :
mdb_val (sizeof (val_a.source) + sizeof (val_a.amount), const_cast<nano::pending_info *> (&val_a))
{
	static_assert (std::is_standard_layout<nano::pending_info>::value, "Standard layout is required");
}

nano::mdb_val::mdb_val (nano::pending_key const & val_a) :
mdb_val (sizeof (val_a), const_cast<nano::pending_key *> (&val_a))
{
	static_assert (std::is_standard_layout<nano::pending_key>::value, "Standard layout is required");
}

nano::mdb_val::mdb_val (nano::unchecked_info const & val_a) :
buffer (std::make_shared<std::vector<uint8_t>> ())
{
	{
		nano::vectorstream stream (*buffer);
		val_a.serialize (stream);
	}
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}

nano::mdb_val::mdb_val (nano::block_info const & val_a) :
mdb_val (sizeof (val_a), const_cast<nano::block_info *> (&val_a))
{
	static_assert (std::is_standard_layout<nano::block_info>::value, "Standard layout is required");
}

nano::mdb_val::mdb_val (nano::endpoint_key const & val_a) :
mdb_val (sizeof (val_a), const_cast<nano::endpoint_key *> (&val_a))
{
	static_assert (std::is_standard_layout<nano::endpoint_key>::value, "Standard layout is required");
}

nano::mdb_val::mdb_val (std::shared_ptr<nano::block> const & val_a) :
buffer (std::make_shared<std::vector<uint8_t>> ())
{
	{
		nano::vectorstream stream (*buffer);
		nano::serialize_block (stream, *val_a);
	}
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}

nano::mdb_val::mdb_val (uint64_t val_a) :
buffer (std::make_shared<std::vector<uint8_t>> ())
{
	{
		boost::endian::native_to_big_inplace (val_a);
		nano::vectorstream stream (*buffer);
		nano::write (stream, val_a);
	}
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}

void * nano::mdb_val::data () const
{
	return value.mv_data;
}

size_t nano::mdb_val::size () const
{
	return value.mv_size;
}

nano::mdb_val::operator nano::account_info () const
{
	nano::account_info result;
	result.epoch = epoch;
	assert (value.mv_size == result.db_size ());
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

nano::mdb_val::operator nano::account_info_v13 () const
{
	nano::account_info_v13 result;
	result.epoch = epoch;
	assert (value.mv_size == result.db_size ());
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

nano::mdb_val::operator nano::block_info () const
{
	nano::block_info result;
	assert (value.mv_size == sizeof (result));
	static_assert (sizeof (nano::block_info::account) + sizeof (nano::block_info::balance) == sizeof (result), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}

nano::mdb_val::operator nano::pending_info () const
{
	nano::pending_info result;
	result.epoch = epoch;
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (nano::pending_info::source) + sizeof (nano::pending_info::amount), reinterpret_cast<uint8_t *> (&result));
	return result;
}

nano::mdb_val::operator nano::pending_key () const
{
	nano::pending_key result;
	assert (value.mv_size == sizeof (result));
	static_assert (sizeof (nano::pending_key::account) + sizeof (nano::pending_key::hash) == sizeof (result), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}

nano::mdb_val::operator nano::unchecked_info () const
{
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	nano::unchecked_info result;
	bool error (result.deserialize (stream));
	assert (!error);
	return result;
}

nano::mdb_val::operator nano::uint128_union () const
{
	nano::uint128_union result;
	assert (size () == sizeof (result));
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
	return result;
}

nano::mdb_val::operator nano::uint256_union () const
{
	nano::uint256_union result;
	assert (size () == sizeof (result));
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
	return result;
}

nano::mdb_val::operator std::array<char, 64> () const
{
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	std::array<char, 64> result;
	auto error = nano::try_read (stream, result);
	assert (!error);
	return result;
}

nano::mdb_val::operator nano::endpoint_key () const
{
	nano::endpoint_key result;
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}

nano::mdb_val::operator nano::no_value () const
{
	return no_value::dummy;
}

nano::mdb_val::operator std::shared_ptr<nano::block> () const
{
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	std::shared_ptr<nano::block> result (nano::deserialize_block (stream));
	return result;
}

nano::mdb_val::operator std::shared_ptr<nano::send_block> () const
{
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<nano::send_block> result (std::make_shared<nano::send_block> (error, stream));
	assert (!error);
	return result;
}

nano::mdb_val::operator std::shared_ptr<nano::receive_block> () const
{
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<nano::receive_block> result (std::make_shared<nano::receive_block> (error, stream));
	assert (!error);
	return result;
}

nano::mdb_val::operator std::shared_ptr<nano::open_block> () const
{
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<nano::open_block> result (std::make_shared<nano::open_block> (error, stream));
	assert (!error);
	return result;
}

nano::mdb_val::operator std::shared_ptr<nano::change_block> () const
{
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<nano::change_block> result (std::make_shared<nano::change_block> (error, stream));
	assert (!error);
	return result;
}

nano::mdb_val::operator std::shared_ptr<nano::state_block> () const
{
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<nano::state_block> result (std::make_shared<nano::state_block> (error, stream));
	assert (!error);
	return result;
}

nano::mdb_val::operator std::shared_ptr<nano::vote> () const
{
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	auto result (nano::make_shared<nano::vote> (error, stream));
	assert (!error);
	return result;
}

nano::mdb_val::operator uint64_t () const
{
	uint64_t result;
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (nano::try_read (stream, result));
	assert (!error);
	boost::endian::big_to_native_inplace (result);
	return result;
}

nano::mdb_val::operator MDB_val * () const
{
	// Allow passing a temporary to a non-c++ function which doesn't have constness
	return const_cast<MDB_val *> (&value);
}

nano::mdb_val::operator MDB_val const & () const
{
	return value;
}

namespace nano
{
/**
 * Fill in our predecessors
 */
class block_predecessor_set : public nano::block_visitor
{
public:
	block_predecessor_set (nano::transaction const & transaction_a, nano::mdb_store & store_a) :
	transaction (transaction_a),
	store (store_a)
	{
	}
	virtual ~block_predecessor_set () = default;
	void fill_value (nano::block const & block_a)
	{
		auto hash (block_a.hash ());
		nano::block_type type;
		auto value (store.block_raw_get (transaction, block_a.previous (), type));
		auto version (store.block_version (transaction, block_a.previous ()));
		assert (value.mv_size != 0);
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.mv_data), static_cast<uint8_t *> (value.mv_data) + value.mv_size);
		std::copy (hash.bytes.begin (), hash.bytes.end (), data.begin () + store.block_successor_offset (transaction, value, type));
		store.block_raw_put (transaction, store.block_database (type, version), block_a.previous (), nano::mdb_val (data.size (), data.data ()));
	}
	void send_block (nano::send_block const & block_a) override
	{
		fill_value (block_a);
	}
	void receive_block (nano::receive_block const & block_a) override
	{
		fill_value (block_a);
	}
	void open_block (nano::open_block const & block_a) override
	{
		// Open blocks don't have a predecessor
	}
	void change_block (nano::change_block const & block_a) override
	{
		fill_value (block_a);
	}
	void state_block (nano::state_block const & block_a) override
	{
		if (!block_a.previous ().is_zero ())
		{
			fill_value (block_a);
		}
	}
	nano::transaction const & transaction;
	nano::mdb_store & store;
};
}

template <typename T, typename U>
nano::mdb_iterator<T, U>::mdb_iterator (nano::transaction const & transaction_a, MDB_dbi db_a, nano::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
	auto status (mdb_cursor_open (tx (transaction_a), db_a, &cursor));
	release_assert (status == 0);
	auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_FIRST));
	release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
	if (status2 != MDB_NOTFOUND)
	{
		auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
		release_assert (status3 == 0 || status3 == MDB_NOTFOUND);
		if (current.first.size () != sizeof (T))
		{
			clear ();
		}
	}
	else
	{
		clear ();
	}
}

template <typename T, typename U>
nano::mdb_iterator<T, U>::mdb_iterator (std::nullptr_t, nano::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
}

template <typename T, typename U>
nano::mdb_iterator<T, U>::mdb_iterator (nano::transaction const & transaction_a, MDB_dbi db_a, MDB_val const & val_a, nano::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
	auto status (mdb_cursor_open (tx (transaction_a), db_a, &cursor));
	release_assert (status == 0);
	current.first = val_a;
	auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_SET_RANGE));
	release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
	if (status2 != MDB_NOTFOUND)
	{
		auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
		release_assert (status3 == 0 || status3 == MDB_NOTFOUND);
		if (current.first.size () != sizeof (T))
		{
			clear ();
		}
	}
	else
	{
		clear ();
	}
}

template <typename T, typename U>
nano::mdb_iterator<T, U>::mdb_iterator (nano::mdb_iterator<T, U> && other_a)
{
	cursor = other_a.cursor;
	other_a.cursor = nullptr;
	current = other_a.current;
}

template <typename T, typename U>
nano::mdb_iterator<T, U>::~mdb_iterator ()
{
	if (cursor != nullptr)
	{
		mdb_cursor_close (cursor);
	}
}

template <typename T, typename U>
nano::store_iterator_impl<T, U> & nano::mdb_iterator<T, U>::operator++ ()
{
	assert (cursor != nullptr);
	auto status (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_NEXT));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	if (status == MDB_NOTFOUND)
	{
		clear ();
	}
	if (current.first.size () != sizeof (T))
	{
		clear ();
	}
	return *this;
}

template <typename T, typename U>
nano::mdb_iterator<T, U> & nano::mdb_iterator<T, U>::operator= (nano::mdb_iterator<T, U> && other_a)
{
	if (cursor != nullptr)
	{
		mdb_cursor_close (cursor);
	}
	cursor = other_a.cursor;
	other_a.cursor = nullptr;
	current = other_a.current;
	other_a.clear ();
	return *this;
}

template <typename T, typename U>
std::pair<nano::mdb_val, nano::mdb_val> * nano::mdb_iterator<T, U>::operator-> ()
{
	return &current;
}

template <typename T, typename U>
bool nano::mdb_iterator<T, U>::operator== (nano::store_iterator_impl<T, U> const & base_a) const
{
	auto const other_a (boost::polymorphic_downcast<nano::mdb_iterator<T, U> const *> (&base_a));
	auto result (current.first.data () == other_a->current.first.data ());
	assert (!result || (current.first.size () == other_a->current.first.size ()));
	assert (!result || (current.second.data () == other_a->current.second.data ()));
	assert (!result || (current.second.size () == other_a->current.second.size ()));
	return result;
}

template <typename T, typename U>
void nano::mdb_iterator<T, U>::clear ()
{
	current.first = nano::mdb_val (current.first.epoch);
	current.second = nano::mdb_val (current.second.epoch);
	assert (is_end_sentinal ());
}

template <typename T, typename U>
MDB_txn * nano::mdb_iterator<T, U>::tx (nano::transaction const & transaction_a) const
{
	return static_cast<MDB_txn *> (transaction_a.get_handle ());
}

template <typename T, typename U>
bool nano::mdb_iterator<T, U>::is_end_sentinal () const
{
	return current.first.size () == 0;
}

template <typename T, typename U>
void nano::mdb_iterator<T, U>::fill (std::pair<T, U> & value_a) const
{
	if (current.first.size () != 0)
	{
		value_a.first = static_cast<T> (current.first);
	}
	else
	{
		value_a.first = T ();
	}
	if (current.second.size () != 0)
	{
		value_a.second = static_cast<U> (current.second);
	}
	else
	{
		value_a.second = U ();
	}
}

template <typename T, typename U>
std::pair<nano::mdb_val, nano::mdb_val> * nano::mdb_merge_iterator<T, U>::operator-> ()
{
	return least_iterator ().operator-> ();
}

template <typename T, typename U>
nano::mdb_merge_iterator<T, U>::mdb_merge_iterator (nano::transaction const & transaction_a, MDB_dbi db1_a, MDB_dbi db2_a) :
impl1 (std::make_unique<nano::mdb_iterator<T, U>> (transaction_a, db1_a, nano::epoch::epoch_0)),
impl2 (std::make_unique<nano::mdb_iterator<T, U>> (transaction_a, db2_a, nano::epoch::epoch_1))
{
}

template <typename T, typename U>
nano::mdb_merge_iterator<T, U>::mdb_merge_iterator (std::nullptr_t) :
impl1 (std::make_unique<nano::mdb_iterator<T, U>> (nullptr, nano::epoch::epoch_0)),
impl2 (std::make_unique<nano::mdb_iterator<T, U>> (nullptr, nano::epoch::epoch_1))
{
}

template <typename T, typename U>
nano::mdb_merge_iterator<T, U>::mdb_merge_iterator (nano::transaction const & transaction_a, MDB_dbi db1_a, MDB_dbi db2_a, MDB_val const & val_a) :
impl1 (std::make_unique<nano::mdb_iterator<T, U>> (transaction_a, db1_a, val_a, nano::epoch::epoch_0)),
impl2 (std::make_unique<nano::mdb_iterator<T, U>> (transaction_a, db2_a, val_a, nano::epoch::epoch_1))
{
}

template <typename T, typename U>
nano::mdb_merge_iterator<T, U>::mdb_merge_iterator (nano::mdb_merge_iterator<T, U> && other_a)
{
	impl1 = std::move (other_a.impl1);
	impl2 = std::move (other_a.impl2);
}

template <typename T, typename U>
nano::mdb_merge_iterator<T, U>::~mdb_merge_iterator ()
{
}

template <typename T, typename U>
nano::store_iterator_impl<T, U> & nano::mdb_merge_iterator<T, U>::operator++ ()
{
	++least_iterator ();
	return *this;
}

template <typename T, typename U>
bool nano::mdb_merge_iterator<T, U>::is_end_sentinal () const
{
	return least_iterator ().is_end_sentinal ();
}

template <typename T, typename U>
void nano::mdb_merge_iterator<T, U>::fill (std::pair<T, U> & value_a) const
{
	auto & current (least_iterator ());
	if (current->first.size () != 0)
	{
		value_a.first = static_cast<T> (current->first);
	}
	else
	{
		value_a.first = T ();
	}
	if (current->second.size () != 0)
	{
		value_a.second = static_cast<U> (current->second);
	}
	else
	{
		value_a.second = U ();
	}
}

template <typename T, typename U>
bool nano::mdb_merge_iterator<T, U>::operator== (nano::store_iterator_impl<T, U> const & base_a) const
{
	assert ((dynamic_cast<nano::mdb_merge_iterator<T, U> const *> (&base_a) != nullptr) && "Incompatible iterator comparison");
	auto & other (static_cast<nano::mdb_merge_iterator<T, U> const &> (base_a));
	return *impl1 == *other.impl1 && *impl2 == *other.impl2;
}

template <typename T, typename U>
nano::mdb_iterator<T, U> & nano::mdb_merge_iterator<T, U>::least_iterator () const
{
	nano::mdb_iterator<T, U> * result;
	if (impl1->is_end_sentinal ())
	{
		result = impl2.get ();
	}
	else if (impl2->is_end_sentinal ())
	{
		result = impl1.get ();
	}
	else
	{
		auto key_cmp (mdb_cmp (mdb_cursor_txn (impl1->cursor), mdb_cursor_dbi (impl1->cursor), impl1->current.first, impl2->current.first));

		if (key_cmp < 0)
		{
			result = impl1.get ();
		}
		else if (key_cmp > 0)
		{
			result = impl2.get ();
		}
		else
		{
			auto val_cmp (mdb_cmp (mdb_cursor_txn (impl1->cursor), mdb_cursor_dbi (impl1->cursor), impl1->current.second, impl2->current.second));
			result = val_cmp < 0 ? impl1.get () : impl2.get ();
		}
	}
	return *result;
}

nano::wallet_value::wallet_value (nano::mdb_val const & val_a)
{
	assert (val_a.size () == sizeof (*this));
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), key.chars.begin ());
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key) + sizeof (work), reinterpret_cast<char *> (&work));
}

nano::wallet_value::wallet_value (nano::uint256_union const & key_a, uint64_t work_a) :
key (key_a),
work (work_a)
{
}

nano::mdb_val nano::wallet_value::val () const
{
	static_assert (sizeof (*this) == sizeof (key) + sizeof (work), "Class not packed");
	return nano::mdb_val (sizeof (*this), const_cast<nano::wallet_value *> (this));
}

template class nano::mdb_iterator<nano::pending_key, nano::pending_info>;
template class nano::mdb_iterator<nano::uint256_union, nano::block_info>;
template class nano::mdb_iterator<nano::uint256_union, nano::uint128_union>;
template class nano::mdb_iterator<nano::uint256_union, nano::uint256_union>;
template class nano::mdb_iterator<nano::uint256_union, std::shared_ptr<nano::block>>;
template class nano::mdb_iterator<nano::uint256_union, std::shared_ptr<nano::vote>>;
template class nano::mdb_iterator<nano::uint256_union, nano::wallet_value>;
template class nano::mdb_iterator<std::array<char, 64>, nano::no_value>;

nano::store_iterator<nano::account, nano::uint128_union> nano::mdb_store::representation_begin (nano::transaction const & transaction_a)
{
	nano::store_iterator<nano::account, nano::uint128_union> result (std::make_unique<nano::mdb_iterator<nano::account, nano::uint128_union>> (transaction_a, representation));
	return result;
}

nano::store_iterator<nano::account, nano::uint128_union> nano::mdb_store::representation_end ()
{
	nano::store_iterator<nano::account, nano::uint128_union> result (nullptr);
	return result;
}

nano::store_iterator<nano::unchecked_key, nano::unchecked_info> nano::mdb_store::unchecked_begin (nano::transaction const & transaction_a)
{
	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> result (std::make_unique<nano::mdb_iterator<nano::unchecked_key, nano::unchecked_info>> (transaction_a, unchecked));
	return result;
}

nano::store_iterator<nano::unchecked_key, nano::unchecked_info> nano::mdb_store::unchecked_begin (nano::transaction const & transaction_a, nano::unchecked_key const & key_a)
{
	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> result (std::make_unique<nano::mdb_iterator<nano::unchecked_key, nano::unchecked_info>> (transaction_a, unchecked, nano::mdb_val (key_a)));
	return result;
}

nano::store_iterator<nano::unchecked_key, nano::unchecked_info> nano::mdb_store::unchecked_end ()
{
	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> result (nullptr);
	return result;
}

nano::store_iterator<nano::account, std::shared_ptr<nano::vote>> nano::mdb_store::vote_begin (nano::transaction const & transaction_a)
{
	return nano::store_iterator<nano::account, std::shared_ptr<nano::vote>> (std::make_unique<nano::mdb_iterator<nano::account, std::shared_ptr<nano::vote>>> (transaction_a, vote));
}

nano::store_iterator<nano::account, std::shared_ptr<nano::vote>> nano::mdb_store::vote_end ()
{
	return nano::store_iterator<nano::account, std::shared_ptr<nano::vote>> (nullptr);
}

nano::mdb_store::mdb_store (bool & error_a, nano::logger_mt & logger_a, boost::filesystem::path const & path_a, nano::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a, int lmdb_max_dbs, bool drop_unchecked, size_t const batch_size) :
logger (logger_a),
env (error_a, path_a, lmdb_max_dbs, true),
mdb_txn_tracker (logger_a, txn_tracking_config_a, block_processor_batch_max_time_a),
txn_tracking_enabled (txn_tracking_config_a.enable)
{
	if (!error_a)
	{
		auto is_fully_upgraded (false);
		{
			auto transaction (tx_begin_read ());
			auto err = mdb_dbi_open (env.tx (transaction), "meta", 0, &meta);
			if (err == MDB_SUCCESS)
			{
				is_fully_upgraded = (version_get (transaction) == version);
				mdb_dbi_close (env, meta);
			}
		}

		// Only open a write lock when upgrades are needed. This is because CLI commands
		// open inactive nodes which can otherwise be locked here if there is a long write
		// (can be a few minutes with the --fastbootstrap flag for instance)
		if (!is_fully_upgraded)
		{
			auto transaction (tx_begin_write ());
			open_databases (error_a, transaction, MDB_CREATE);
			if (!error_a)
			{
				error_a |= do_upgrades (transaction, batch_size);
			}
		}
		else
		{
			auto transaction (tx_begin_read ());
			open_databases (error_a, transaction, 0);
		}

		if (!error_a && drop_unchecked)
		{
			auto transaction (tx_begin_write ());
			unchecked_clear (transaction);
		}
	}
}

void nano::mdb_store::serialize_mdb_tracker (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time)
{
	mdb_txn_tracker.serialize_json (json, min_read_time, min_write_time);
}

nano::write_transaction nano::mdb_store::tx_begin_write ()
{
	return env.tx_begin_write (create_txn_callbacks ());
}

nano::read_transaction nano::mdb_store::tx_begin_read ()
{
	return env.tx_begin_read (create_txn_callbacks ());
}

nano::mdb_txn_callbacks nano::mdb_store::create_txn_callbacks ()
{
	nano::mdb_txn_callbacks mdb_txn_callbacks;
	if (txn_tracking_enabled)
	{
		// clang-format off
		mdb_txn_callbacks.txn_start = ([&mdb_txn_tracker = mdb_txn_tracker](const nano::transaction_impl * transaction_impl) {
			mdb_txn_tracker.add (transaction_impl);
		});
		mdb_txn_callbacks.txn_end = ([&mdb_txn_tracker = mdb_txn_tracker](const nano::transaction_impl * transaction_impl) {
			mdb_txn_tracker.erase (transaction_impl);
		});
		// clang-format on
	}
	return mdb_txn_callbacks;
}

/**
 * This is only used with testing. If using a different store version than the latest then you may need
 * to modify some of the objects in the store to be appropriate for the version before an upgrade.
 */
void nano::mdb_store::initialize (nano::transaction const & transaction_a, nano::genesis const & genesis_a)
{
	auto hash_l (genesis_a.hash ());
	assert (latest_v0_begin (transaction_a) == latest_v0_end ());
	assert (latest_v1_begin (transaction_a) == latest_v1_end ());
	nano::block_sideband sideband (nano::block_type::open, network_params.ledger.genesis_account, 0, network_params.ledger.genesis_amount, 1, nano::seconds_since_epoch ());
	block_put (transaction_a, hash_l, *genesis_a.open, sideband);
	account_put (transaction_a, network_params.ledger.genesis_account, { hash_l, genesis_a.open->hash (), genesis_a.open->hash (), std::numeric_limits<nano::uint128_t>::max (), nano::seconds_since_epoch (), 1, 1, nano::epoch::epoch_0 });
	representation_put (transaction_a, network_params.ledger.genesis_account, std::numeric_limits<nano::uint128_t>::max ());
	frontier_put (transaction_a, hash_l, network_params.ledger.genesis_account);
}

void nano::mdb_store::open_databases (bool & error_a, nano::transaction const & transaction_a, unsigned flags)
{
	error_a |= mdb_dbi_open (env.tx (transaction_a), "frontiers", flags, &frontiers) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "accounts", flags, &accounts_v0) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "accounts_v1", flags, &accounts_v1) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "send", flags, &send_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "receive", flags, &receive_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "open", flags, &open_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "change", flags, &change_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "state", flags, &state_blocks_v0) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "state_v1", flags, &state_blocks_v1) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "pending", flags, &pending_v0) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "pending_v1", flags, &pending_v1) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "representation", flags, &representation) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "unchecked", flags, &unchecked) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "vote", flags, &vote) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "online_weight", flags, &online_weight) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "meta", flags, &meta) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "peers", flags, &peers) != 0;
	if (!full_sideband (transaction_a))
	{
		error_a |= mdb_dbi_open (env.tx (transaction_a), "blocks_info", flags, &blocks_info) != 0;
	}
}

void nano::mdb_store::version_put (nano::transaction const & transaction_a, int version_a)
{
	nano::uint256_union version_key (1);
	nano::uint256_union version_value (version_a);
	auto status (mdb_put (env.tx (transaction_a), meta, nano::mdb_val (version_key), nano::mdb_val (version_value), 0));
	release_assert (status == 0);
	if (blocks_info == 0 && !full_sideband (transaction_a))
	{
		auto status (mdb_dbi_open (env.tx (transaction_a), "blocks_info", MDB_CREATE, &blocks_info));
		release_assert (status == MDB_SUCCESS);
	}
	if (blocks_info != 0 && full_sideband (transaction_a))
	{
		auto status (mdb_drop (env.tx (transaction_a), blocks_info, 1));
		release_assert (status == MDB_SUCCESS);
		blocks_info = 0;
	}
}

int nano::mdb_store::version_get (nano::transaction const & transaction_a) const
{
	nano::uint256_union version_key (1);
	nano::mdb_val data;
	auto error (mdb_get (env.tx (transaction_a), meta, nano::mdb_val (version_key), data));
	int result (1);
	if (error != MDB_NOTFOUND)
	{
		nano::uint256_union version_value (data);
		assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
		result = version_value.number ().convert_to<int> ();
	}
	return result;
}

void nano::mdb_store::peer_put (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a)
{
	nano::mdb_val zero (static_cast<uint64_t> (0));
	auto status (mdb_put (env.tx (transaction_a), peers, nano::mdb_val (endpoint_a), zero, 0));
	release_assert (status == 0);
}

void nano::mdb_store::peer_del (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a)
{
	auto status (mdb_del (env.tx (transaction_a), peers, nano::mdb_val (endpoint_a), nullptr));
	release_assert (status == 0);
}

bool nano::mdb_store::peer_exists (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) const
{
	nano::mdb_val junk;
	auto status (mdb_get (env.tx (transaction_a), peers, nano::mdb_val (endpoint_a), junk));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	return (status == 0);
}

size_t nano::mdb_store::peer_count (nano::transaction const & transaction_a) const
{
	MDB_stat stats;
	auto status (mdb_stat (env.tx (transaction_a), peers, &stats));
	release_assert (status == 0);
	return stats.ms_entries;
}

void nano::mdb_store::peer_clear (nano::transaction const & transaction_a)
{
	auto status (mdb_drop (env.tx (transaction_a), peers, 0));
	release_assert (status == 0);
}

nano::store_iterator<nano::endpoint_key, nano::no_value> nano::mdb_store::peers_begin (nano::transaction const & transaction_a)
{
	nano::store_iterator<nano::endpoint_key, nano::no_value> result (std::make_unique<nano::mdb_iterator<nano::endpoint_key, nano::no_value>> (transaction_a, peers));
	return result;
}

nano::store_iterator<nano::endpoint_key, nano::no_value> nano::mdb_store::peers_end ()
{
	nano::store_iterator<nano::endpoint_key, nano::no_value> result (nano::store_iterator<nano::endpoint_key, nano::no_value> (nullptr));
	return result;
}

bool nano::mdb_store::do_upgrades (nano::write_transaction & transaction_a, size_t batch_size)
{
	auto error (false);
	auto version_l = version_get (transaction_a);
	switch (version_l)
	{
		case 1:
			upgrade_v1_to_v2 (transaction_a);
		case 2:
			upgrade_v2_to_v3 (transaction_a);
		case 3:
			upgrade_v3_to_v4 (transaction_a);
		case 4:
			upgrade_v4_to_v5 (transaction_a);
		case 5:
			upgrade_v5_to_v6 (transaction_a);
		case 6:
			upgrade_v6_to_v7 (transaction_a);
		case 7:
			upgrade_v7_to_v8 (transaction_a);
		case 8:
			upgrade_v8_to_v9 (transaction_a);
		case 9:
			upgrade_v9_to_v10 (transaction_a);
		case 10:
			upgrade_v10_to_v11 (transaction_a);
		case 11:
			upgrade_v11_to_v12 (transaction_a);
		case 12:
			upgrade_v12_to_v13 (transaction_a, batch_size);
		case 13:
			upgrade_v13_to_v14 (transaction_a);
		case 14:
			break;
		default:
			logger.always_log (boost::str (boost::format ("The version of the ledger (%1%) is too high for this node") % version_l));
			error = true;
			break;
	}
	return error;
}

void nano::mdb_store::upgrade_v1_to_v2 (nano::transaction const & transaction_a)
{
	version_put (transaction_a, 2);
	nano::account account (1);
	while (!account.is_zero ())
	{
		nano::mdb_iterator<nano::uint256_union, nano::account_info_v1> i (transaction_a, accounts_v0, nano::mdb_val (account));
		std::cerr << std::hex;
		if (i != nano::mdb_iterator<nano::uint256_union, nano::account_info_v1> (nullptr))
		{
			account = nano::uint256_union (i->first);
			nano::account_info_v1 v1 (i->second);
			nano::account_info_v5 v2;
			v2.balance = v1.balance;
			v2.head = v1.head;
			v2.modified = v1.modified;
			v2.rep_block = v1.rep_block;
			auto block (block_get (transaction_a, v1.head));
			while (!block->previous ().is_zero ())
			{
				block = block_get (transaction_a, block->previous ());
			}
			v2.open_block = block->hash ();
			auto status (mdb_put (env.tx (transaction_a), accounts_v0, nano::mdb_val (account), nano::mdb_val (sizeof (v2), &v2), 0));
			release_assert (status == 0);
			account = account.number () + 1;
		}
		else
		{
			account.clear ();
		}
	}
}

void nano::mdb_store::upgrade_v2_to_v3 (nano::transaction const & transaction_a)
{
	version_put (transaction_a, 3);
	mdb_drop (env.tx (transaction_a), representation, 0);
	for (auto i (std::make_unique<nano::mdb_iterator<nano::account, nano::account_info_v5>> (transaction_a, accounts_v0)), n (std::make_unique<nano::mdb_iterator<nano::account, nano::account_info_v5>> (nullptr)); *i != *n; ++(*i))
	{
		nano::account account_l ((*i)->first);
		nano::account_info_v5 info ((*i)->second);
		representative_visitor visitor (transaction_a, *this);
		visitor.compute (info.head);
		assert (!visitor.result.is_zero ());
		info.rep_block = visitor.result;
		auto impl (boost::polymorphic_downcast<nano::mdb_iterator<nano::account, nano::account_info_v5> *> (i.get ()));
		mdb_cursor_put (impl->cursor, nano::mdb_val (account_l), nano::mdb_val (sizeof (info), &info), MDB_CURRENT);
		representation_add (transaction_a, visitor.result, info.balance.number ());
	}
}

void nano::mdb_store::upgrade_v3_to_v4 (nano::transaction const & transaction_a)
{
	version_put (transaction_a, 4);
	std::queue<std::pair<nano::pending_key, nano::pending_info>> items;
	for (auto i (nano::store_iterator<nano::block_hash, nano::pending_info_v3> (std::make_unique<nano::mdb_iterator<nano::block_hash, nano::pending_info_v3>> (transaction_a, pending_v0))), n (nano::store_iterator<nano::block_hash, nano::pending_info_v3> (nullptr)); i != n; ++i)
	{
		nano::block_hash const & hash (i->first);
		nano::pending_info_v3 const & info (i->second);
		items.push (std::make_pair (nano::pending_key (info.destination, hash), nano::pending_info (info.source, info.amount, nano::epoch::epoch_0)));
	}
	mdb_drop (env.tx (transaction_a), pending_v0, 0);
	while (!items.empty ())
	{
		pending_put (transaction_a, items.front ().first, items.front ().second);
		items.pop ();
	}
}

void nano::mdb_store::upgrade_v4_to_v5 (nano::transaction const & transaction_a)
{
	version_put (transaction_a, 5);
	for (auto i (nano::store_iterator<nano::account, nano::account_info_v5> (std::make_unique<nano::mdb_iterator<nano::account, nano::account_info_v5>> (transaction_a, accounts_v0))), n (nano::store_iterator<nano::account, nano::account_info_v5> (nullptr)); i != n; ++i)
	{
		nano::account_info_v5 const & info (i->second);
		nano::block_hash successor (0);
		auto block (block_get (transaction_a, info.head));
		while (block != nullptr)
		{
			auto hash (block->hash ());
			if (block_successor (transaction_a, hash).is_zero () && !successor.is_zero ())
			{
				std::vector<uint8_t> vector;
				{
					nano::vectorstream stream (vector);
					block->serialize (stream);
					nano::write (stream, successor.bytes);
				}
				block_raw_put (transaction_a, block_database (block->type (), nano::epoch::epoch_0), hash, { vector.size (), vector.data () });
				if (!block->previous ().is_zero ())
				{
					nano::block_type type;
					auto value (block_raw_get (transaction_a, block->previous (), type));
					auto version (block_version (transaction_a, block->previous ()));
					assert (value.mv_size != 0);
					std::vector<uint8_t> data (static_cast<uint8_t *> (value.mv_data), static_cast<uint8_t *> (value.mv_data) + value.mv_size);
					std::copy (hash.bytes.begin (), hash.bytes.end (), data.end () - nano::block_sideband::size (type));
					block_raw_put (transaction_a, block_database (type, version), block->previous (), nano::mdb_val (data.size (), data.data ()));
				}
			}
			successor = hash;
			block = block_get (transaction_a, block->previous ());
		}
	}
}

void nano::mdb_store::upgrade_v5_to_v6 (nano::transaction const & transaction_a)
{
	version_put (transaction_a, 6);
	std::deque<std::pair<nano::account, nano::account_info_v13>> headers;
	for (auto i (nano::store_iterator<nano::account, nano::account_info_v5> (std::make_unique<nano::mdb_iterator<nano::account, nano::account_info_v5>> (transaction_a, accounts_v0))), n (nano::store_iterator<nano::account, nano::account_info_v5> (nullptr)); i != n; ++i)
	{
		nano::account const & account (i->first);
		nano::account_info_v5 info_old (i->second);
		uint64_t block_count (0);
		auto hash (info_old.head);
		while (!hash.is_zero ())
		{
			++block_count;
			auto block (block_get (transaction_a, hash));
			assert (block != nullptr);
			hash = block->previous ();
		}
		headers.emplace_back (account, nano::account_info_v13{ info_old.head, info_old.rep_block, info_old.open_block, info_old.balance, info_old.modified, block_count, nano::epoch::epoch_0 });
	}
	for (auto i (headers.begin ()), n (headers.end ()); i != n; ++i)
	{
		auto status (mdb_put (env.tx (transaction_a), accounts_v0, nano::mdb_val (i->first), nano::mdb_val (i->second), 0));
		release_assert (status == 0);
	}
}

void nano::mdb_store::upgrade_v6_to_v7 (nano::transaction const & transaction_a)
{
	version_put (transaction_a, 7);
	mdb_drop (env.tx (transaction_a), unchecked, 0);
}

void nano::mdb_store::upgrade_v7_to_v8 (nano::transaction const & transaction_a)
{
	version_put (transaction_a, 8);
	mdb_drop (env.tx (transaction_a), unchecked, 1);
	mdb_dbi_open (env.tx (transaction_a), "unchecked", MDB_CREATE | MDB_DUPSORT, &unchecked);
}

void nano::mdb_store::upgrade_v8_to_v9 (nano::transaction const & transaction_a)
{
	version_put (transaction_a, 9);
	MDB_dbi sequence;
	mdb_dbi_open (env.tx (transaction_a), "sequence", MDB_CREATE | MDB_DUPSORT, &sequence);
	nano::genesis genesis;
	std::shared_ptr<nano::block> block (std::move (genesis.open));
	nano::keypair junk;
	for (nano::mdb_iterator<nano::account, uint64_t> i (transaction_a, sequence), n (nano::mdb_iterator<nano::account, uint64_t> (nullptr)); i != n; ++i)
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
		uint64_t sequence;
		auto error (nano::try_read (stream, sequence));
		// Create a dummy vote with the same sequence number for easy upgrading.  This won't have a valid signature.
		nano::vote dummy (nano::account (i->first), junk.prv, sequence, block);
		std::vector<uint8_t> vector;
		{
			nano::vectorstream stream (vector);
			dummy.serialize (stream);
		}
		auto status1 (mdb_put (env.tx (transaction_a), vote, nano::mdb_val (i->first), nano::mdb_val (vector.size (), vector.data ()), 0));
		release_assert (status1 == 0);
		assert (!error);
	}
	mdb_drop (env.tx (transaction_a), sequence, 1);
}

void nano::mdb_store::upgrade_v9_to_v10 (nano::transaction const & transaction_a)
{
}

void nano::mdb_store::upgrade_v10_to_v11 (nano::transaction const & transaction_a)
{
	version_put (transaction_a, 11);
	MDB_dbi unsynced;
	mdb_dbi_open (env.tx (transaction_a), "unsynced", MDB_CREATE | MDB_DUPSORT, &unsynced);
	mdb_drop (env.tx (transaction_a), unsynced, 1);
}

void nano::mdb_store::upgrade_v11_to_v12 (nano::transaction const & transaction_a)
{
	version_put (transaction_a, 12);
	mdb_drop (env.tx (transaction_a), unchecked, 1);
	mdb_dbi_open (env.tx (transaction_a), "unchecked", MDB_CREATE, &unchecked);
	MDB_dbi checksum;
	mdb_dbi_open (env.tx (transaction_a), "checksum", MDB_CREATE, &checksum);
	mdb_drop (env.tx (transaction_a), checksum, 1);
}

void nano::mdb_store::upgrade_v12_to_v13 (nano::write_transaction & transaction_a, size_t const batch_size)
{
	size_t cost (0);
	nano::account account (0);
	auto const & not_an_account (network_params.random.not_an_account);
	while (account != not_an_account)
	{
		nano::account first (0);
		nano::account_info_v13 second;
		{
			nano::store_iterator<nano::account, nano::account_info_v13> current (std::make_unique<nano::mdb_merge_iterator<nano::account, nano::account_info_v13>> (transaction_a, accounts_v0, accounts_v1, nano::mdb_val (account)));
			nano::store_iterator<nano::account, nano::account_info_v13> end (nullptr);
			if (current != end)
			{
				first = current->first;
				second = current->second;
			}
		}
		if (!first.is_zero ())
		{
			auto hash (second.open_block);
			uint64_t height (1);
			nano::block_sideband sideband;
			while (!hash.is_zero ())
			{
				if (cost >= batch_size)
				{
					logger.always_log (boost::str (boost::format ("Upgrading sideband information for account %1%... height %2%") % first.to_account ().substr (0, 24) % std::to_string (height)));
					transaction_a.commit ();
					std::this_thread::yield ();
					transaction_a.renew ();
					cost = 0;
				}
				auto block (block_get (transaction_a, hash, &sideband));
				assert (block != nullptr);
				if (sideband.height == 0)
				{
					sideband.height = height;
					block_put (transaction_a, hash, *block, sideband, block_version (transaction_a, hash));
					cost += 16;
				}
				else
				{
					cost += 1;
				}
				hash = sideband.successor;
				++height;
			}
			account = first.number () + 1;
		}
		else
		{
			account = not_an_account;
		}
	}
	if (account == not_an_account)
	{
		logger.always_log ("Completed sideband upgrade");
		version_put (transaction_a, 13);
	}
}

void nano::mdb_store::upgrade_v13_to_v14 (nano::transaction const & transaction_a)
{
	// Upgrade all accounts to have a confirmation of 0 (except genesis which should have 1)
	version_put (transaction_a, 14);
	nano::store_iterator<nano::account, nano::account_info_v13> i (std::make_unique<nano::mdb_merge_iterator<nano::account, nano::account_info_v13>> (transaction_a, accounts_v0, accounts_v1));
	nano::store_iterator<nano::account, nano::account_info_v13> n (nullptr);

	std::vector<std::pair<nano::account, nano::account_info>> account_infos;
	account_infos.reserve (account_count (transaction_a));
	for (; i != n; ++i)
	{
		nano::account_info_v13 const & account_info_v13 (i->second);
		uint64_t confirmation_height = 0;
		if (i->first == network_params.ledger.genesis_account)
		{
			confirmation_height = 1;
		}
		account_infos.emplace_back (i->first, nano::account_info{ account_info_v13.head, account_info_v13.rep_block, account_info_v13.open_block, account_info_v13.balance, account_info_v13.modified, account_info_v13.block_count, confirmation_height, account_info_v13.epoch });
	}

	for (auto const & account_info : account_infos)
	{
		account_put (transaction_a, account_info.first, account_info.second);
	}

	logger.always_log ("Completed confirmation height upgrade");

	nano::uint256_union node_id_mdb_key (3);
	auto error (mdb_del (env.tx (transaction_a), meta, nano::mdb_val (node_id_mdb_key), nullptr));
	release_assert (!error || error == MDB_NOTFOUND);
}

void nano::mdb_store::clear (MDB_dbi db_a)
{
	auto transaction (tx_begin_write ());
	auto status (mdb_drop (env.tx (transaction), db_a, 0));
	release_assert (status == 0);
}

nano::uint128_t nano::mdb_store::block_balance (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	nano::block_sideband sideband;
	auto block (block_get (transaction_a, hash_a, &sideband));
	nano::uint128_t result (block_balance_calculated (block, sideband));
	return result;
}

nano::uint128_t nano::mdb_store::block_balance_computed (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	assert (!full_sideband (transaction_a));
	summation_visitor visitor (transaction_a, *this);
	return visitor.compute_balance (hash_a);
}

nano::epoch nano::mdb_store::block_version (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	nano::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), state_blocks_v1, nano::mdb_val (hash_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	return status == 0 ? nano::epoch::epoch_1 : nano::epoch::epoch_0;
}

void nano::mdb_store::representation_add (nano::transaction const & transaction_a, nano::block_hash const & source_a, nano::uint128_t const & amount_a)
{
	auto source_block (block_get (transaction_a, source_a));
	assert (source_block != nullptr);
	auto source_rep (source_block->representative ());
	auto source_previous (representation_get (transaction_a, source_rep));
	representation_put (transaction_a, source_rep, source_previous + amount_a);
}

MDB_dbi nano::mdb_store::block_database (nano::block_type type_a, nano::epoch epoch_a)
{
	if (type_a == nano::block_type::state)
	{
		assert (epoch_a == nano::epoch::epoch_0 || epoch_a == nano::epoch::epoch_1);
	}
	else
	{
		assert (epoch_a == nano::epoch::epoch_0);
	}
	MDB_dbi result;
	switch (type_a)
	{
		case nano::block_type::send:
			result = send_blocks;
			break;
		case nano::block_type::receive:
			result = receive_blocks;
			break;
		case nano::block_type::open:
			result = open_blocks;
			break;
		case nano::block_type::change:
			result = change_blocks;
			break;
		case nano::block_type::state:
			switch (epoch_a)
			{
				case nano::epoch::epoch_0:
					result = state_blocks_v0;
					break;
				case nano::epoch::epoch_1:
					result = state_blocks_v1;
					break;
				default:
					assert (false);
			}
			break;
		default:
			assert (false);
			break;
	}
	return result;
}

void nano::mdb_store::block_raw_put (nano::transaction const & transaction_a, MDB_dbi database_a, nano::block_hash const & hash_a, MDB_val value_a)
{
	auto status2 (mdb_put (env.tx (transaction_a), database_a, nano::mdb_val (hash_a), &value_a, 0));
	release_assert (status2 == 0);
}

void nano::mdb_store::block_put (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block const & block_a, nano::block_sideband const & sideband_a, nano::epoch epoch_a)
{
	assert (block_a.type () == sideband_a.type);
	assert (sideband_a.successor.is_zero () || block_exists (transaction_a, sideband_a.successor));
	std::vector<uint8_t> vector;
	{
		nano::vectorstream stream (vector);
		block_a.serialize (stream);
		sideband_a.serialize (stream);
	}
	block_raw_put (transaction_a, block_database (block_a.type (), epoch_a), hash_a, { vector.size (), vector.data () });
	nano::block_predecessor_set predecessor (transaction_a, *this);
	block_a.visit (predecessor);
	assert (block_a.previous ().is_zero () || block_successor (transaction_a, block_a.previous ()) == hash_a);
}

boost::optional<MDB_val> nano::mdb_store::block_raw_get_by_type (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_type & type_a) const
{
	nano::mdb_val value;
	auto status (MDB_NOTFOUND);
	switch (type_a)
	{
		case nano::block_type::send:
		{
			status = mdb_get (env.tx (transaction_a), send_blocks, nano::mdb_val (hash_a), value);
			break;
		}
		case nano::block_type::receive:
		{
			status = mdb_get (env.tx (transaction_a), receive_blocks, nano::mdb_val (hash_a), value);
			break;
		}
		case nano::block_type::open:
		{
			status = mdb_get (env.tx (transaction_a), open_blocks, nano::mdb_val (hash_a), value);
			break;
		}
		case nano::block_type::change:
		{
			status = mdb_get (env.tx (transaction_a), change_blocks, nano::mdb_val (hash_a), value);
			break;
		}
		case nano::block_type::state:
		{
			status = mdb_get (env.tx (transaction_a), state_blocks_v1, nano::mdb_val (hash_a), value);
			if (status != 0)
			{
				status = mdb_get (env.tx (transaction_a), state_blocks_v0, nano::mdb_val (hash_a), value);
			}
			break;
		}
		case nano::block_type::invalid:
		case nano::block_type::not_a_block:
		{
			break;
		}
	}

	release_assert (status == MDB_SUCCESS || status == MDB_NOTFOUND);
	boost::optional<MDB_val> result;
	if (status == MDB_SUCCESS)
	{
		result = value;
	}

	return result;
}

MDB_val nano::mdb_store::block_raw_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_type & type_a) const
{
	nano::mdb_val result;
	// Table lookups are ordered by match probability
	nano::block_type block_types[]{ nano::block_type::state, nano::block_type::send, nano::block_type::receive, nano::block_type::open, nano::block_type::change };
	for (auto current_type : block_types)
	{
		auto mdb_val (block_raw_get_by_type (transaction_a, hash_a, current_type));
		if (mdb_val.is_initialized ())
		{
			type_a = current_type;
			result = mdb_val.get ();
			break;
		}
	}

	return result;
}

template <typename T>
std::shared_ptr<nano::block> nano::mdb_store::block_random (nano::transaction const & transaction_a, MDB_dbi database)
{
	nano::block_hash hash;
	nano::random_pool::generate_block (hash.bytes.data (), hash.bytes.size ());
	nano::store_iterator<nano::block_hash, std::shared_ptr<T>> existing (std::make_unique<nano::mdb_iterator<nano::block_hash, std::shared_ptr<T>>> (transaction_a, database, nano::mdb_val (hash)));
	if (existing == nano::store_iterator<nano::block_hash, std::shared_ptr<T>> (nullptr))
	{
		existing = nano::store_iterator<nano::block_hash, std::shared_ptr<T>> (std::make_unique<nano::mdb_iterator<nano::block_hash, std::shared_ptr<T>>> (transaction_a, database));
	}
	auto end (nano::store_iterator<nano::block_hash, std::shared_ptr<T>> (nullptr));
	assert (existing != end);
	return block_get (transaction_a, nano::block_hash (existing->first));
}

std::shared_ptr<nano::block> nano::mdb_store::block_random (nano::transaction const & transaction_a)
{
	auto count (block_count (transaction_a));
	release_assert (std::numeric_limits<CryptoPP::word32>::max () > count.sum ());
	auto region = static_cast<size_t> (nano::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (count.sum () - 1)));
	std::shared_ptr<nano::block> result;
	if (region < count.send)
	{
		result = block_random<nano::send_block> (transaction_a, send_blocks);
	}
	else
	{
		region -= count.send;
		if (region < count.receive)
		{
			result = block_random<nano::receive_block> (transaction_a, receive_blocks);
		}
		else
		{
			region -= count.receive;
			if (region < count.open)
			{
				result = block_random<nano::open_block> (transaction_a, open_blocks);
			}
			else
			{
				region -= count.open;
				if (region < count.change)
				{
					result = block_random<nano::change_block> (transaction_a, change_blocks);
				}
				else
				{
					region -= count.change;
					if (region < count.state_v0)
					{
						result = block_random<nano::state_block> (transaction_a, state_blocks_v0);
					}
					else
					{
						result = block_random<nano::state_block> (transaction_a, state_blocks_v1);
					}
				}
			}
		}
	}
	assert (result != nullptr);
	return result;
}

bool nano::mdb_store::full_sideband (nano::transaction const & transaction_a) const
{
	return version_get (transaction_a) > 12;
}

bool nano::mdb_store::entry_has_sideband (MDB_val entry_a, nano::block_type type_a) const
{
	return entry_a.mv_size == nano::block::size (type_a) + nano::block_sideband::size (type_a);
}

size_t nano::mdb_store::block_successor_offset (nano::transaction const & transaction_a, MDB_val entry_a, nano::block_type type_a) const
{
	size_t result;
	if (full_sideband (transaction_a) || entry_has_sideband (entry_a, type_a))
	{
		result = entry_a.mv_size - nano::block_sideband::size (type_a);
	}
	else
	{
		// Read old successor-only sideband
		assert (entry_a.mv_size == nano::block::size (type_a) + sizeof (nano::uint256_union));
		result = entry_a.mv_size - sizeof (nano::uint256_union);
	}
	return result;
}

nano::block_hash nano::mdb_store::block_successor (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	nano::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	nano::block_hash result;
	if (value.mv_size != 0)
	{
		assert (value.mv_size >= result.bytes.size ());
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data) + block_successor_offset (transaction_a, value, type), result.bytes.size ());
		auto error (nano::try_read (stream, result.bytes));
		assert (!error);
	}
	else
	{
		result.clear ();
	}
	return result;
}

void nano::mdb_store::block_successor_clear (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	nano::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	auto version (block_version (transaction_a, hash_a));
	assert (value.mv_size != 0);
	std::vector<uint8_t> data (static_cast<uint8_t *> (value.mv_data), static_cast<uint8_t *> (value.mv_data) + value.mv_size);
	std::fill_n (data.begin () + block_successor_offset (transaction_a, value, type), sizeof (nano::uint256_union), 0);
	block_raw_put (transaction_a, block_database (type, version), hash_a, nano::mdb_val (data.size (), data.data ()));
}

// Converts a block hash to a block height
uint64_t nano::mdb_store::block_account_height (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	nano::block_sideband sideband;
	auto block = block_get (transaction_a, hash_a, &sideband);
	assert (block != nullptr);
	return sideband.height;
}

std::shared_ptr<nano::block> nano::mdb_store::block_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_sideband * sideband_a) const
{
	nano::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	std::shared_ptr<nano::block> result;
	if (value.mv_size != 0)
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
		result = nano::deserialize_block (stream, type);
		assert (result != nullptr);
		if (sideband_a)
		{
			sideband_a->type = type;
			if (full_sideband (transaction_a) || entry_has_sideband (value, type))
			{
				auto error (sideband_a->deserialize (stream));
				assert (!error);
			}
			else
			{
				// Reconstruct sideband data for block.
				sideband_a->account = block_account_computed (transaction_a, hash_a);
				sideband_a->balance = block_balance_computed (transaction_a, hash_a);
				sideband_a->successor = block_successor (transaction_a, hash_a);
				sideband_a->height = 0;
				sideband_a->timestamp = 0;
			}
		}
	}
	return result;
}

void nano::mdb_store::block_del (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto status (mdb_del (env.tx (transaction_a), state_blocks_v1, nano::mdb_val (hash_a), nullptr));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	if (status != 0)
	{
		auto status (mdb_del (env.tx (transaction_a), state_blocks_v0, nano::mdb_val (hash_a), nullptr));
		release_assert (status == 0 || status == MDB_NOTFOUND);
		if (status != 0)
		{
			auto status (mdb_del (env.tx (transaction_a), send_blocks, nano::mdb_val (hash_a), nullptr));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			if (status != 0)
			{
				auto status (mdb_del (env.tx (transaction_a), receive_blocks, nano::mdb_val (hash_a), nullptr));
				release_assert (status == 0 || status == MDB_NOTFOUND);
				if (status != 0)
				{
					auto status (mdb_del (env.tx (transaction_a), open_blocks, nano::mdb_val (hash_a), nullptr));
					release_assert (status == 0 || status == MDB_NOTFOUND);
					if (status != 0)
					{
						auto status (mdb_del (env.tx (transaction_a), change_blocks, nano::mdb_val (hash_a), nullptr));
						release_assert (status == 0);
					}
				}
			}
		}
	}
}

bool nano::mdb_store::block_exists (nano::transaction const & transaction_a, nano::block_type type, nano::block_hash const & hash_a)
{
	auto exists (false);
	nano::mdb_val junk;

	switch (type)
	{
		case nano::block_type::send:
		{
			auto status (mdb_get (env.tx (transaction_a), send_blocks, nano::mdb_val (hash_a), junk));
			assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			break;
		}
		case nano::block_type::receive:
		{
			auto status (mdb_get (env.tx (transaction_a), receive_blocks, nano::mdb_val (hash_a), junk));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			break;
		}
		case nano::block_type::open:
		{
			auto status (mdb_get (env.tx (transaction_a), open_blocks, nano::mdb_val (hash_a), junk));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			break;
		}
		case nano::block_type::change:
		{
			auto status (mdb_get (env.tx (transaction_a), change_blocks, nano::mdb_val (hash_a), junk));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			break;
		}
		case nano::block_type::state:
		{
			auto status (mdb_get (env.tx (transaction_a), state_blocks_v0, nano::mdb_val (hash_a), junk));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			if (!exists)
			{
				auto status (mdb_get (env.tx (transaction_a), state_blocks_v1, nano::mdb_val (hash_a), junk));
				release_assert (status == 0 || status == MDB_NOTFOUND);
				exists = status == 0;
			}
			break;
		}
		case nano::block_type::invalid:
		case nano::block_type::not_a_block:
			break;
	}

	return exists;
}

bool nano::mdb_store::block_exists (nano::transaction const & tx_a, nano::block_hash const & hash_a)
{
	// Table lookups are ordered by match probability
	// clang-format off
	return
		block_exists (tx_a, nano::block_type::state, hash_a) ||
		block_exists (tx_a, nano::block_type::send, hash_a) ||
		block_exists (tx_a, nano::block_type::receive, hash_a) ||
		block_exists (tx_a, nano::block_type::open, hash_a) ||
		block_exists (tx_a, nano::block_type::change, hash_a);
	// clang-format on
}

nano::block_counts nano::mdb_store::block_count (nano::transaction const & transaction_a)
{
	nano::block_counts result;
	MDB_stat send_stats;
	auto status1 (mdb_stat (env.tx (transaction_a), send_blocks, &send_stats));
	release_assert (status1 == 0);
	MDB_stat receive_stats;
	auto status2 (mdb_stat (env.tx (transaction_a), receive_blocks, &receive_stats));
	release_assert (status2 == 0);
	MDB_stat open_stats;
	auto status3 (mdb_stat (env.tx (transaction_a), open_blocks, &open_stats));
	release_assert (status3 == 0);
	MDB_stat change_stats;
	auto status4 (mdb_stat (env.tx (transaction_a), change_blocks, &change_stats));
	release_assert (status4 == 0);
	MDB_stat state_v0_stats;
	auto status5 (mdb_stat (env.tx (transaction_a), state_blocks_v0, &state_v0_stats));
	release_assert (status5 == 0);
	MDB_stat state_v1_stats;
	auto status6 (mdb_stat (env.tx (transaction_a), state_blocks_v1, &state_v1_stats));
	release_assert (status6 == 0);
	result.send = send_stats.ms_entries;
	result.receive = receive_stats.ms_entries;
	result.open = open_stats.ms_entries;
	result.change = change_stats.ms_entries;
	result.state_v0 = state_v0_stats.ms_entries;
	result.state_v1 = state_v1_stats.ms_entries;
	return result;
}

bool nano::mdb_store::root_exists (nano::transaction const & transaction_a, nano::uint256_union const & root_a)
{
	return block_exists (transaction_a, root_a) || account_exists (transaction_a, root_a);
}

bool nano::mdb_store::source_exists (nano::transaction const & transaction_a, nano::block_hash const & source_a)
{
	return block_exists (transaction_a, nano::block_type::state, source_a) || block_exists (transaction_a, nano::block_type::send, source_a);
}

nano::account nano::mdb_store::block_account (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	nano::block_sideband sideband;
	auto block (block_get (transaction_a, hash_a, &sideband));
	nano::account result (block->account ());
	if (result.is_zero ())
	{
		result = sideband.account;
	}
	assert (!result.is_zero ());
	return result;
}

// Return account containing hash
nano::account nano::mdb_store::block_account_computed (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	assert (!full_sideband (transaction_a));
	nano::account result (0);
	auto hash (hash_a);
	while (result.is_zero ())
	{
		auto block (block_get (transaction_a, hash));
		assert (block);
		result = block->account ();
		if (result.is_zero ())
		{
			auto type (nano::block_type::invalid);
			auto value (block_raw_get (transaction_a, block->previous (), type));
			if (entry_has_sideband (value, type))
			{
				result = block_account (transaction_a, block->previous ());
			}
			else
			{
				nano::block_info block_info;
				if (!block_info_get (transaction_a, hash, block_info))
				{
					result = block_info.account;
				}
				else
				{
					result = frontier_get (transaction_a, hash);
					if (result.is_zero ())
					{
						auto successor (block_successor (transaction_a, hash));
						assert (!successor.is_zero ());
						hash = successor;
					}
				}
			}
		}
	}
	assert (!result.is_zero ());
	return result;
}

void nano::mdb_store::account_del (nano::transaction const & transaction_a, nano::account const & account_a)
{
	auto status1 (mdb_del (env.tx (transaction_a), accounts_v1, nano::mdb_val (account_a), nullptr));
	if (status1 != 0)
	{
		release_assert (status1 == MDB_NOTFOUND);
		auto status2 (mdb_del (env.tx (transaction_a), accounts_v0, nano::mdb_val (account_a), nullptr));
		release_assert (status2 == 0);
	}
}

bool nano::mdb_store::account_exists (nano::transaction const & transaction_a, nano::account const & account_a)
{
	auto iterator (latest_begin (transaction_a, account_a));
	return iterator != latest_end () && nano::account (iterator->first) == account_a;
}

bool nano::mdb_store::account_get (nano::transaction const & transaction_a, nano::account const & account_a, nano::account_info & info_a)
{
	nano::mdb_val value;
	auto status1 (mdb_get (env.tx (transaction_a), accounts_v1, nano::mdb_val (account_a), value));
	release_assert (status1 == 0 || status1 == MDB_NOTFOUND);
	bool result (false);
	nano::epoch epoch;
	if (status1 == 0)
	{
		epoch = nano::epoch::epoch_1;
	}
	else
	{
		auto status2 (mdb_get (env.tx (transaction_a), accounts_v0, nano::mdb_val (account_a), value));
		release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
		if (status2 == 0)
		{
			epoch = nano::epoch::epoch_0;
		}
		else
		{
			result = true;
		}
	}
	if (!result)
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		info_a.epoch = epoch;
		result = info_a.deserialize (stream);
	}
	return result;
}

void nano::mdb_store::frontier_put (nano::transaction const & transaction_a, nano::block_hash const & block_a, nano::account const & account_a)
{
	auto status (mdb_put (env.tx (transaction_a), frontiers, nano::mdb_val (block_a), nano::mdb_val (account_a), 0));
	release_assert (status == 0);
}

nano::account nano::mdb_store::frontier_get (nano::transaction const & transaction_a, nano::block_hash const & block_a) const
{
	nano::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), frontiers, nano::mdb_val (block_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	nano::account result (0);
	if (status == 0)
	{
		result = nano::uint256_union (value);
	}
	return result;
}

void nano::mdb_store::frontier_del (nano::transaction const & transaction_a, nano::block_hash const & block_a)
{
	auto status (mdb_del (env.tx (transaction_a), frontiers, nano::mdb_val (block_a), nullptr));
	release_assert (status == 0);
}

size_t nano::mdb_store::account_count (nano::transaction const & transaction_a)
{
	MDB_stat stats1;
	auto status1 (mdb_stat (env.tx (transaction_a), accounts_v0, &stats1));
	release_assert (status1 == 0);
	MDB_stat stats2;
	auto status2 (mdb_stat (env.tx (transaction_a), accounts_v1, &stats2));
	release_assert (status2 == 0);
	auto result (stats1.ms_entries + stats2.ms_entries);
	return result;
}

MDB_dbi nano::mdb_store::get_account_db (nano::epoch epoch_a) const
{
	MDB_dbi db;
	switch (epoch_a)
	{
		case nano::epoch::invalid:
		case nano::epoch::unspecified:
			assert (false);
		case nano::epoch::epoch_0:
			db = accounts_v0;
			break;
		case nano::epoch::epoch_1:
			db = accounts_v1;
			break;
	}
	return db;
}

MDB_dbi nano::mdb_store::get_pending_db (nano::epoch epoch_a) const
{
	MDB_dbi db;
	switch (epoch_a)
	{
		case nano::epoch::invalid:
		case nano::epoch::unspecified:
			assert (false);
		case nano::epoch::epoch_0:
			db = pending_v0;
			break;
		case nano::epoch::epoch_1:
			db = pending_v1;
			break;
	}
	return db;
}

void nano::mdb_store::account_put (nano::transaction const & transaction_a, nano::account const & account_a, nano::account_info const & info_a)
{
	auto status (mdb_put (env.tx (transaction_a), get_account_db (info_a.epoch), nano::mdb_val (account_a), nano::mdb_val (info_a), 0));
	release_assert (status == 0);
}

void nano::mdb_store::confirmation_height_clear (nano::transaction const & transaction_a, nano::account const & account, nano::account_info const & account_info)
{
	nano::account_info info_copy (account_info);
	if (info_copy.confirmation_height > 0)
	{
		info_copy.confirmation_height = 0;
		account_put (transaction_a, account, info_copy);
	}
}

void nano::mdb_store::confirmation_height_clear (nano::transaction const & transaction_a)
{
	for (auto i (latest_begin (transaction_a)), n (latest_end ()); i != n; ++i)
	{
		confirmation_height_clear (transaction_a, i->first, i->second);
	}
}

uint64_t nano::mdb_store::cemented_count (nano::transaction const & transaction_a)
{
	uint64_t sum = 0;
	for (auto i (latest_begin (transaction_a)), n (latest_end ()); i != n; ++i)
	{
		nano::account_info const & info (i->second);
		sum += info.confirmation_height;
	}
	return sum;
}

void nano::mdb_store::pending_put (nano::transaction const & transaction_a, nano::pending_key const & key_a, nano::pending_info const & pending_a)
{
	auto status (mdb_put (env.tx (transaction_a), get_pending_db (pending_a.epoch), nano::mdb_val (key_a), nano::mdb_val (pending_a), 0));
	release_assert (status == 0);
}

void nano::mdb_store::pending_del (nano::transaction const & transaction_a, nano::pending_key const & key_a)
{
	auto status1 (mdb_del (env.tx (transaction_a), pending_v1, mdb_val (key_a), nullptr));
	if (status1 != 0)
	{
		release_assert (status1 == MDB_NOTFOUND);
		auto status2 (mdb_del (env.tx (transaction_a), pending_v0, mdb_val (key_a), nullptr));
		release_assert (status2 == 0);
	}
}

bool nano::mdb_store::pending_exists (nano::transaction const & transaction_a, nano::pending_key const & key_a)
{
	auto iterator (pending_begin (transaction_a, key_a));
	return iterator != pending_end () && nano::pending_key (iterator->first) == key_a;
}

bool nano::mdb_store::pending_get (nano::transaction const & transaction_a, nano::pending_key const & key_a, nano::pending_info & pending_a)
{
	nano::mdb_val value;
	auto status1 (mdb_get (env.tx (transaction_a), pending_v1, mdb_val (key_a), value));
	release_assert (status1 == 0 || status1 == MDB_NOTFOUND);
	bool result (false);
	nano::epoch epoch;
	if (status1 == 0)
	{
		epoch = nano::epoch::epoch_1;
	}
	else
	{
		auto status2 (mdb_get (env.tx (transaction_a), pending_v0, mdb_val (key_a), value));
		release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
		if (status2 == 0)
		{
			epoch = nano::epoch::epoch_0;
		}
		else
		{
			result = true;
		}
	}
	if (!result)
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		pending_a.epoch = epoch;
		result = pending_a.deserialize (stream);
	}
	return result;
}

nano::store_iterator<nano::pending_key, nano::pending_info> nano::mdb_store::pending_begin (nano::transaction const & transaction_a, nano::pending_key const & key_a)
{
	nano::store_iterator<nano::pending_key, nano::pending_info> result (std::make_unique<nano::mdb_merge_iterator<nano::pending_key, nano::pending_info>> (transaction_a, pending_v0, pending_v1, mdb_val (key_a)));
	return result;
}

nano::store_iterator<nano::pending_key, nano::pending_info> nano::mdb_store::pending_begin (nano::transaction const & transaction_a)
{
	nano::store_iterator<nano::pending_key, nano::pending_info> result (std::make_unique<nano::mdb_merge_iterator<nano::pending_key, nano::pending_info>> (transaction_a, pending_v0, pending_v1));
	return result;
}

nano::store_iterator<nano::pending_key, nano::pending_info> nano::mdb_store::pending_end ()
{
	nano::store_iterator<nano::pending_key, nano::pending_info> result (nullptr);
	return result;
}

nano::store_iterator<nano::pending_key, nano::pending_info> nano::mdb_store::pending_v0_begin (nano::transaction const & transaction_a, nano::pending_key const & key_a)
{
	nano::store_iterator<nano::pending_key, nano::pending_info> result (std::make_unique<nano::mdb_iterator<nano::pending_key, nano::pending_info>> (transaction_a, pending_v0, mdb_val (key_a)));
	return result;
}

nano::store_iterator<nano::pending_key, nano::pending_info> nano::mdb_store::pending_v0_begin (nano::transaction const & transaction_a)
{
	nano::store_iterator<nano::pending_key, nano::pending_info> result (std::make_unique<nano::mdb_iterator<nano::pending_key, nano::pending_info>> (transaction_a, pending_v0));
	return result;
}

nano::store_iterator<nano::pending_key, nano::pending_info> nano::mdb_store::pending_v0_end ()
{
	nano::store_iterator<nano::pending_key, nano::pending_info> result (nullptr);
	return result;
}

nano::store_iterator<nano::pending_key, nano::pending_info> nano::mdb_store::pending_v1_begin (nano::transaction const & transaction_a, nano::pending_key const & key_a)
{
	nano::store_iterator<nano::pending_key, nano::pending_info> result (std::make_unique<nano::mdb_iterator<nano::pending_key, nano::pending_info>> (transaction_a, pending_v1, mdb_val (key_a)));
	return result;
}

nano::store_iterator<nano::pending_key, nano::pending_info> nano::mdb_store::pending_v1_begin (nano::transaction const & transaction_a)
{
	nano::store_iterator<nano::pending_key, nano::pending_info> result (std::make_unique<nano::mdb_iterator<nano::pending_key, nano::pending_info>> (transaction_a, pending_v1));
	return result;
}

nano::store_iterator<nano::pending_key, nano::pending_info> nano::mdb_store::pending_v1_end ()
{
	nano::store_iterator<nano::pending_key, nano::pending_info> result (nullptr);
	return result;
}

bool nano::mdb_store::block_info_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_info & block_info_a) const
{
	assert (!full_sideband (transaction_a));
	nano::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), blocks_info, nano::mdb_val (hash_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	bool result (true);
	if (status != MDB_NOTFOUND)
	{
		result = false;
		assert (value.size () == sizeof (block_info_a.account.bytes) + sizeof (block_info_a.balance.bytes));
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error1 (nano::try_read (stream, block_info_a.account));
		assert (!error1);
		auto error2 (nano::try_read (stream, block_info_a.balance));
		assert (!error2);
	}
	return result;
}

nano::uint128_t nano::mdb_store::representation_get (nano::transaction const & transaction_a, nano::account const & account_a)
{
	nano::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), representation, nano::mdb_val (account_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	nano::uint128_t result = 0;
	if (status == 0)
	{
		nano::uint128_union rep;
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error (nano::try_read (stream, rep));
		assert (!error);
		result = rep.number ();
	}
	return result;
}

void nano::mdb_store::representation_put (nano::transaction const & transaction_a, nano::account const & account_a, nano::uint128_t const & representation_a)
{
	nano::uint128_union rep (representation_a);
	auto status (mdb_put (env.tx (transaction_a), representation, nano::mdb_val (account_a), nano::mdb_val (rep), 0));
	release_assert (status == 0);
}

void nano::mdb_store::unchecked_clear (nano::transaction const & transaction_a)
{
	auto status (mdb_drop (env.tx (transaction_a), unchecked, 0));
	release_assert (status == 0);
}

void nano::mdb_store::unchecked_put (nano::transaction const & transaction_a, nano::unchecked_key const & key_a, nano::unchecked_info const & info_a)
{
	auto status (mdb_put (env.tx (transaction_a), unchecked, nano::mdb_val (key_a), nano::mdb_val (info_a), 0));
	release_assert (status == 0);
}

void nano::mdb_store::unchecked_put (nano::transaction const & transaction_a, nano::block_hash const & hash_a, std::shared_ptr<nano::block> const & block_a)
{
	nano::unchecked_key key (hash_a, block_a->hash ());
	nano::unchecked_info info (block_a, block_a->account (), nano::seconds_since_epoch (), nano::signature_verification::unknown);
	unchecked_put (transaction_a, key, info);
}

std::shared_ptr<nano::vote> nano::mdb_store::vote_get (nano::transaction const & transaction_a, nano::account const & account_a)
{
	nano::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), vote, nano::mdb_val (account_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	if (status == 0)
	{
		std::shared_ptr<nano::vote> result (value);
		assert (result != nullptr);
		return result;
	}
	return nullptr;
}

std::vector<nano::unchecked_info> nano::mdb_store::unchecked_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	std::vector<nano::unchecked_info> result;
	for (auto i (unchecked_begin (transaction_a, nano::unchecked_key (hash_a, 0))), n (unchecked_end ()); i != n && nano::block_hash (i->first.key ()) == hash_a; ++i)
	{
		nano::unchecked_info const & unchecked_info (i->second);
		result.push_back (unchecked_info);
	}
	return result;
}

void nano::mdb_store::unchecked_del (nano::transaction const & transaction_a, nano::unchecked_key const & key_a)
{
	auto status (mdb_del (env.tx (transaction_a), unchecked, nano::mdb_val (key_a), nullptr));
	release_assert (status == 0 || status == MDB_NOTFOUND);
}

size_t nano::mdb_store::unchecked_count (nano::transaction const & transaction_a)
{
	MDB_stat unchecked_stats;
	auto status (mdb_stat (env.tx (transaction_a), unchecked, &unchecked_stats));
	release_assert (status == 0);
	auto result (unchecked_stats.ms_entries);
	return result;
}

void nano::mdb_store::online_weight_put (nano::transaction const & transaction_a, uint64_t time_a, nano::amount const & amount_a)
{
	auto status (mdb_put (env.tx (transaction_a), online_weight, nano::mdb_val (time_a), nano::mdb_val (amount_a), 0));
	release_assert (status == 0);
}

void nano::mdb_store::online_weight_del (nano::transaction const & transaction_a, uint64_t time_a)
{
	auto status (mdb_del (env.tx (transaction_a), online_weight, nano::mdb_val (time_a), nullptr));
	release_assert (status == 0);
}

nano::store_iterator<uint64_t, nano::amount> nano::mdb_store::online_weight_begin (nano::transaction const & transaction_a)
{
	return nano::store_iterator<uint64_t, nano::amount> (std::make_unique<nano::mdb_iterator<uint64_t, nano::amount>> (transaction_a, online_weight));
}

nano::store_iterator<uint64_t, nano::amount> nano::mdb_store::online_weight_end ()
{
	return nano::store_iterator<uint64_t, nano::amount> (nullptr);
}

size_t nano::mdb_store::online_weight_count (nano::transaction const & transaction_a) const
{
	MDB_stat online_weight_stats;
	auto status1 (mdb_stat (env.tx (transaction_a), online_weight, &online_weight_stats));
	release_assert (status1 == 0);
	return online_weight_stats.ms_entries;
}

void nano::mdb_store::online_weight_clear (nano::transaction const & transaction_a)
{
	auto status (mdb_drop (env.tx (transaction_a), online_weight, 0));
	release_assert (status == 0);
}

void nano::mdb_store::flush (nano::transaction const & transaction_a)
{
	{
		std::lock_guard<std::mutex> lock (cache_mutex);
		vote_cache_l1.swap (vote_cache_l2);
		vote_cache_l1.clear ();
	}
	for (auto i (vote_cache_l2.begin ()), n (vote_cache_l2.end ()); i != n; ++i)
	{
		std::vector<uint8_t> vector;
		{
			nano::vectorstream stream (vector);
			i->second->serialize (stream);
		}
		auto status1 (mdb_put (env.tx (transaction_a), vote, nano::mdb_val (i->first), nano::mdb_val (vector.size (), vector.data ()), 0));
		release_assert (status1 == 0);
	}
}
std::shared_ptr<nano::vote> nano::mdb_store::vote_current (nano::transaction const & transaction_a, nano::account const & account_a)
{
	assert (!cache_mutex.try_lock ());
	std::shared_ptr<nano::vote> result;
	auto existing (vote_cache_l1.find (account_a));
	auto have_existing (true);
	if (existing == vote_cache_l1.end ())
	{
		existing = vote_cache_l2.find (account_a);
		if (existing == vote_cache_l2.end ())
		{
			have_existing = false;
		}
	}
	if (have_existing)
	{
		result = existing->second;
	}
	else
	{
		result = vote_get (transaction_a, account_a);
	}
	return result;
}

std::shared_ptr<nano::vote> nano::mdb_store::vote_generate (nano::transaction const & transaction_a, nano::account const & account_a, nano::raw_key const & key_a, std::shared_ptr<nano::block> block_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto result (vote_current (transaction_a, account_a));
	uint64_t sequence ((result ? result->sequence : 0) + 1);
	result = std::make_shared<nano::vote> (account_a, key_a, sequence, block_a);
	vote_cache_l1[account_a] = result;
	return result;
}

std::shared_ptr<nano::vote> nano::mdb_store::vote_generate (nano::transaction const & transaction_a, nano::account const & account_a, nano::raw_key const & key_a, std::vector<nano::block_hash> blocks_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto result (vote_current (transaction_a, account_a));
	uint64_t sequence ((result ? result->sequence : 0) + 1);
	result = std::make_shared<nano::vote> (account_a, key_a, sequence, blocks_a);
	vote_cache_l1[account_a] = result;
	return result;
}

std::shared_ptr<nano::vote> nano::mdb_store::vote_max (nano::transaction const & transaction_a, std::shared_ptr<nano::vote> vote_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto current (vote_current (transaction_a, vote_a->account));
	auto result (vote_a);
	if (current != nullptr && current->sequence > result->sequence)
	{
		result = current;
	}
	vote_cache_l1[vote_a->account] = result;
	return result;
}

nano::store_iterator<nano::account, nano::account_info> nano::mdb_store::latest_begin (nano::transaction const & transaction_a, nano::account const & account_a)
{
	nano::store_iterator<nano::account, nano::account_info> result (std::make_unique<nano::mdb_merge_iterator<nano::account, nano::account_info>> (transaction_a, accounts_v0, accounts_v1, nano::mdb_val (account_a)));
	return result;
}

nano::store_iterator<nano::account, nano::account_info> nano::mdb_store::latest_begin (nano::transaction const & transaction_a)
{
	nano::store_iterator<nano::account, nano::account_info> result (std::make_unique<nano::mdb_merge_iterator<nano::account, nano::account_info>> (transaction_a, accounts_v0, accounts_v1));
	return result;
}

nano::store_iterator<nano::account, nano::account_info> nano::mdb_store::latest_end ()
{
	nano::store_iterator<nano::account, nano::account_info> result (nullptr);
	return result;
}

nano::store_iterator<nano::account, nano::account_info> nano::mdb_store::latest_v0_begin (nano::transaction const & transaction_a, nano::account const & account_a)
{
	nano::store_iterator<nano::account, nano::account_info> result (std::make_unique<nano::mdb_iterator<nano::account, nano::account_info>> (transaction_a, accounts_v0, nano::mdb_val (account_a)));
	return result;
}

nano::store_iterator<nano::account, nano::account_info> nano::mdb_store::latest_v0_begin (nano::transaction const & transaction_a)
{
	nano::store_iterator<nano::account, nano::account_info> result (std::make_unique<nano::mdb_iterator<nano::account, nano::account_info>> (transaction_a, accounts_v0));
	return result;
}

nano::store_iterator<nano::account, nano::account_info> nano::mdb_store::latest_v0_end ()
{
	nano::store_iterator<nano::account, nano::account_info> result (nullptr);
	return result;
}

nano::store_iterator<nano::account, nano::account_info> nano::mdb_store::latest_v1_begin (nano::transaction const & transaction_a, nano::account const & account_a)
{
	nano::store_iterator<nano::account, nano::account_info> result (std::make_unique<nano::mdb_iterator<nano::account, nano::account_info>> (transaction_a, accounts_v1, nano::mdb_val (account_a)));
	return result;
}

nano::store_iterator<nano::account, nano::account_info> nano::mdb_store::latest_v1_begin (nano::transaction const & transaction_a)
{
	nano::store_iterator<nano::account, nano::account_info> result (std::make_unique<nano::mdb_iterator<nano::account, nano::account_info>> (transaction_a, accounts_v1));
	return result;
}

nano::store_iterator<nano::account, nano::account_info> nano::mdb_store::latest_v1_end ()
{
	nano::store_iterator<nano::account, nano::account_info> result (nullptr);
	return result;
}
