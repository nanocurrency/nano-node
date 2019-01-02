#include <nano/node/lmdb.hpp>

#include <nano/lib/utility.hpp>
#include <nano/node/common.hpp>
#include <nano/secure/versioning.hpp>

#include <boost/polymorphic_cast.hpp>

#include <queue>

nano::mdb_env::mdb_env (bool & error_a, boost::filesystem::path const & path_a, int max_dbs)
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
			auto status2 (mdb_env_set_maxdbs (environment, max_dbs));
			release_assert (status2 == 0);
			auto status3 (mdb_env_set_mapsize (environment, 1ULL * 1024 * 1024 * 1024 * 128)); // 128 Gigabyte
			release_assert (status3 == 0);
			// It seems if there's ever more threads than mdb_env_set_maxreaders has read slots available, we get failures on transaction creation unless MDB_NOTLS is specified
			// This can happen if something like 256 io_threads are specified in the node config
			auto status4 (mdb_env_open (environment, path_a.string ().c_str (), MDB_NOSUBDIR | MDB_NOTLS, 00600));
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

nano::transaction nano::mdb_env::tx_begin (bool write_a) const
{
	return { std::make_unique<nano::mdb_txn> (*this, write_a) };
}

MDB_txn * nano::mdb_env::tx (nano::transaction const & transaction_a) const
{
	auto result (boost::polymorphic_downcast<nano::mdb_txn *> (transaction_a.impl.get ()));
	release_assert (mdb_txn_env (result->handle) == environment);
	return *result;
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

nano::mdb_val::mdb_val (nano::pending_info const & val_a) :
mdb_val (sizeof (val_a.source) + sizeof (val_a.amount), const_cast<nano::pending_info *> (&val_a))
{
}

nano::mdb_val::mdb_val (nano::pending_key const & val_a) :
mdb_val (sizeof (val_a), const_cast<nano::pending_key *> (&val_a))
{
}

nano::mdb_val::mdb_val (nano::block_info const & val_a) :
mdb_val (sizeof (val_a), const_cast<nano::block_info *> (&val_a))
{
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
	nano::read (stream, result);
	return result;
}

nano::mdb_val::operator no_value () const
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
	std::shared_ptr<nano::vote> result (std::make_shared<nano::vote> (error, stream));
	assert (!error);
	return result;
}

nano::mdb_val::operator uint64_t () const
{
	uint64_t result;
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (nano::read (stream, result));
	assert (!error);
	return result;
}

nano::mdb_val::operator MDB_val * () const
{
	// Allow passing a temporary to a non-c++ function which doesn't have constness
	return const_cast<MDB_val *> (&value);
};

nano::mdb_val::operator MDB_val const & () const
{
	return value;
}

nano::mdb_txn::mdb_txn (nano::mdb_env const & environment_a, bool write_a)
{
	auto status (mdb_txn_begin (environment_a, nullptr, write_a ? 0 : MDB_RDONLY, &handle));
	release_assert (status == 0);
}

nano::mdb_txn::~mdb_txn ()
{
	auto status (mdb_txn_commit (handle));
	release_assert (status == 0);
}

nano::mdb_txn::operator MDB_txn * () const
{
	return handle;
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
		std::copy (hash.bytes.begin (), hash.bytes.end (), data.end () - hash.bytes.size ());
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
	auto result (boost::polymorphic_downcast<nano::mdb_txn *> (transaction_a.impl.get ()));
	return *result;
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
template class nano::mdb_iterator<std::array<char, 64>, nano::mdb_val::no_value>;

nano::store_iterator<nano::block_hash, nano::block_info> nano::mdb_store::block_info_begin (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	nano::store_iterator<nano::block_hash, nano::block_info> result (std::make_unique<nano::mdb_iterator<nano::block_hash, nano::block_info>> (transaction_a, blocks_info, nano::mdb_val (hash_a)));
	return result;
}

nano::store_iterator<nano::block_hash, nano::block_info> nano::mdb_store::block_info_begin (nano::transaction const & transaction_a)
{
	nano::store_iterator<nano::block_hash, nano::block_info> result (std::make_unique<nano::mdb_iterator<nano::block_hash, nano::block_info>> (transaction_a, blocks_info));
	return result;
}

nano::store_iterator<nano::block_hash, nano::block_info> nano::mdb_store::block_info_end ()
{
	nano::store_iterator<nano::block_hash, nano::block_info> result (nullptr);
	return result;
}

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

nano::store_iterator<nano::unchecked_key, std::shared_ptr<nano::block>> nano::mdb_store::unchecked_begin (nano::transaction const & transaction_a)
{
	nano::store_iterator<nano::unchecked_key, std::shared_ptr<nano::block>> result (std::make_unique<nano::mdb_iterator<nano::unchecked_key, std::shared_ptr<nano::block>>> (transaction_a, unchecked));
	return result;
}

nano::store_iterator<nano::unchecked_key, std::shared_ptr<nano::block>> nano::mdb_store::unchecked_begin (nano::transaction const & transaction_a, nano::unchecked_key const & key_a)
{
	nano::store_iterator<nano::unchecked_key, std::shared_ptr<nano::block>> result (std::make_unique<nano::mdb_iterator<nano::unchecked_key, std::shared_ptr<nano::block>>> (transaction_a, unchecked, nano::mdb_val (key_a)));
	return result;
}

nano::store_iterator<nano::unchecked_key, std::shared_ptr<nano::block>> nano::mdb_store::unchecked_end ()
{
	nano::store_iterator<nano::unchecked_key, std::shared_ptr<nano::block>> result (nullptr);
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

nano::mdb_store::mdb_store (bool & error_a, nano::logging & logging_a, boost::filesystem::path const & path_a, int lmdb_max_dbs) :
logging (logging_a),
env (error_a, path_a, lmdb_max_dbs),
frontiers (0),
accounts_v0 (0),
accounts_v1 (0),
send_blocks (0),
receive_blocks (0),
open_blocks (0),
change_blocks (0),
state_blocks_v0 (0),
state_blocks_v1 (0),
pending_v0 (0),
pending_v1 (0),
blocks_info (0),
representation (0),
unchecked (0),
checksum (0),
vote (0),
meta (0)
{
	if (!error_a)
	{
		auto transaction (tx_begin_write ());
		error_a |= mdb_dbi_open (env.tx (transaction), "frontiers", MDB_CREATE, &frontiers) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "accounts", MDB_CREATE, &accounts_v0) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "accounts_v1", MDB_CREATE, &accounts_v1) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "send", MDB_CREATE, &send_blocks) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "receive", MDB_CREATE, &receive_blocks) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "open", MDB_CREATE, &open_blocks) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "change", MDB_CREATE, &change_blocks) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "state", MDB_CREATE, &state_blocks_v0) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "state_v1", MDB_CREATE, &state_blocks_v1) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "pending", MDB_CREATE, &pending_v0) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "pending_v1", MDB_CREATE, &pending_v1) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "blocks_info", MDB_CREATE, &blocks_info) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "representation", MDB_CREATE, &representation) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "unchecked", MDB_CREATE, &unchecked) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "checksum", MDB_CREATE, &checksum) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "vote", MDB_CREATE, &vote) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "meta", MDB_CREATE, &meta) != 0;
		if (!error_a)
		{
			do_upgrades (transaction);
			checksum_put (transaction, 0, 0, 0);
		}
	}
}

nano::transaction nano::mdb_store::tx_begin_write ()
{
	return tx_begin (true);
}

nano::transaction nano::mdb_store::tx_begin_read ()
{
	return tx_begin (false);
}

nano::transaction nano::mdb_store::tx_begin (bool write_a)
{
	return env.tx_begin (write_a);
}

void nano::mdb_store::initialize (nano::transaction const & transaction_a, nano::genesis const & genesis_a)
{
	auto hash_l (genesis_a.hash ());
	assert (latest_v0_begin (transaction_a) == latest_v0_end ());
	assert (latest_v1_begin (transaction_a) == latest_v1_end ());
	block_put (transaction_a, hash_l, *genesis_a.open);
	account_put (transaction_a, genesis_account, { hash_l, genesis_a.open->hash (), genesis_a.open->hash (), std::numeric_limits<nano::uint128_t>::max (), nano::seconds_since_epoch (), 1, nano::epoch::epoch_0 });
	representation_put (transaction_a, genesis_account, std::numeric_limits<nano::uint128_t>::max ());
	checksum_put (transaction_a, 0, 0, hash_l);
	frontier_put (transaction_a, hash_l, genesis_account);
}

void nano::mdb_store::version_put (nano::transaction const & transaction_a, int version_a)
{
	nano::uint256_union version_key (1);
	nano::uint256_union version_value (version_a);
	auto status (mdb_put (env.tx (transaction_a), meta, nano::mdb_val (version_key), nano::mdb_val (version_value), 0));
	release_assert (status == 0);
}

int nano::mdb_store::version_get (nano::transaction const & transaction_a)
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

nano::raw_key nano::mdb_store::get_node_id (nano::transaction const & transaction_a)
{
	nano::uint256_union node_id_mdb_key (3);
	nano::raw_key node_id;
	nano::mdb_val value;
	auto error (mdb_get (env.tx (transaction_a), meta, nano::mdb_val (node_id_mdb_key), value));
	if (!error)
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		error = nano::read (stream, node_id.data);
		assert (!error);
	}
	if (error)
	{
		nano::random_pool.GenerateBlock (node_id.data.bytes.data (), node_id.data.bytes.size ());
		error = mdb_put (env.tx (transaction_a), meta, nano::mdb_val (node_id_mdb_key), nano::mdb_val (node_id.data), 0);
	}
	assert (!error);
	return node_id;
}

void nano::mdb_store::delete_node_id (nano::transaction const & transaction_a)
{
	nano::uint256_union node_id_mdb_key (3);
	auto error (mdb_del (env.tx (transaction_a), meta, nano::mdb_val (node_id_mdb_key), nullptr));
	assert (!error || error == MDB_NOTFOUND);
}

void nano::mdb_store::do_upgrades (nano::transaction const & transaction_a)
{
	switch (version_get (transaction_a))
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
			break;
		default:
			assert (false);
	}
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
			auto status (mdb_put (env.tx (transaction_a), accounts_v0, nano::mdb_val (account), v2.val (), 0));
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
		mdb_cursor_put (impl->cursor, nano::mdb_val (account_l), info.val (), MDB_CURRENT);
		representation_add (transaction_a, visitor.result, info.balance.number ());
	}
}

void nano::mdb_store::upgrade_v3_to_v4 (nano::transaction const & transaction_a)
{
	version_put (transaction_a, 4);
	std::queue<std::pair<nano::pending_key, nano::pending_info>> items;
	for (auto i (nano::store_iterator<nano::block_hash, nano::pending_info_v3> (std::make_unique<nano::mdb_iterator<nano::block_hash, nano::pending_info_v3>> (transaction_a, pending_v0))), n (nano::store_iterator<nano::block_hash, nano::pending_info_v3> (nullptr)); i != n; ++i)
	{
		nano::block_hash hash (i->first);
		nano::pending_info_v3 info (i->second);
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
		nano::account_info_v5 info (i->second);
		nano::block_hash successor (0);
		auto block (block_get (transaction_a, info.head));
		while (block != nullptr)
		{
			auto hash (block->hash ());
			if (block_successor (transaction_a, hash).is_zero () && !successor.is_zero ())
			{
				block_put (transaction_a, hash, *block, successor);
			}
			successor = hash;
			block = block_get (transaction_a, block->previous ());
		}
	}
}

void nano::mdb_store::upgrade_v5_to_v6 (nano::transaction const & transaction_a)
{
	version_put (transaction_a, 6);
	std::deque<std::pair<nano::account, nano::account_info>> headers;
	for (auto i (nano::store_iterator<nano::account, nano::account_info_v5> (std::make_unique<nano::mdb_iterator<nano::account, nano::account_info_v5>> (transaction_a, accounts_v0))), n (nano::store_iterator<nano::account, nano::account_info_v5> (nullptr)); i != n; ++i)
	{
		nano::account account (i->first);
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
		nano::account_info info (info_old.head, info_old.rep_block, info_old.open_block, info_old.balance, info_old.modified, block_count, nano::epoch::epoch_0);
		headers.push_back (std::make_pair (account, info));
	}
	for (auto i (headers.begin ()), n (headers.end ()); i != n; ++i)
	{
		account_put (transaction_a, i->first, i->second);
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
		auto error (nano::read (stream, sequence));
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
	//std::cerr << boost::str (boost::format ("Performing database upgrade to version 10...\n"));
	version_put (transaction_a, 10);
	for (auto i (latest_v0_begin (transaction_a)), n (latest_v0_end ()); i != n; ++i)
	{
		nano::account_info info (i->second);
		if (info.block_count >= block_info_max)
		{
			nano::account account (i->first);
			//std::cerr << boost::str (boost::format ("Upgrading account %1%...\n") % account.to_account ());
			size_t block_count (1);
			auto hash (info.open_block);
			while (!hash.is_zero ())
			{
				if ((block_count % block_info_max) == 0)
				{
					nano::block_info block_info;
					block_info.account = account;
					nano::amount balance (block_balance (transaction_a, hash));
					block_info.balance = balance;
					block_info_put (transaction_a, hash, block_info);
				}
				hash = block_successor (transaction_a, hash);
				++block_count;
			}
		}
	}
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
}

void nano::mdb_store::clear (MDB_dbi db_a)
{
	auto transaction (tx_begin_write ());
	auto status (mdb_drop (env.tx (transaction), db_a, 0));
	release_assert (status == 0);
}

nano::uint128_t nano::mdb_store::block_balance (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
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

void nano::mdb_store::block_put (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block const & block_a, nano::block_hash const & successor_a, nano::epoch epoch_a)
{
	assert (successor_a.is_zero () || block_exists (transaction_a, successor_a));
	std::vector<uint8_t> vector;
	{
		nano::vectorstream stream (vector);
		block_a.serialize (stream);
		nano::write (stream, successor_a.bytes);
	}
	block_raw_put (transaction_a, block_database (block_a.type (), epoch_a), hash_a, { vector.size (), vector.data () });
	nano::block_predecessor_set predecessor (transaction_a, *this);
	block_a.visit (predecessor);
	assert (block_a.previous ().is_zero () || block_successor (transaction_a, block_a.previous ()) == hash_a);
}

MDB_val nano::mdb_store::block_raw_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_type & type_a)
{
	nano::mdb_val result;
	auto status (mdb_get (env.tx (transaction_a), send_blocks, nano::mdb_val (hash_a), result));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	if (status != 0)
	{
		auto status (mdb_get (env.tx (transaction_a), receive_blocks, nano::mdb_val (hash_a), result));
		release_assert (status == 0 || status == MDB_NOTFOUND);
		if (status != 0)
		{
			auto status (mdb_get (env.tx (transaction_a), open_blocks, nano::mdb_val (hash_a), result));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			if (status != 0)
			{
				auto status (mdb_get (env.tx (transaction_a), change_blocks, nano::mdb_val (hash_a), result));
				release_assert (status == 0 || status == MDB_NOTFOUND);
				if (status != 0)
				{
					auto status (mdb_get (env.tx (transaction_a), state_blocks_v0, nano::mdb_val (hash_a), result));
					release_assert (status == 0 || status == MDB_NOTFOUND);
					if (status != 0)
					{
						auto status (mdb_get (env.tx (transaction_a), state_blocks_v1, nano::mdb_val (hash_a), result));
						release_assert (status == 0 || status == MDB_NOTFOUND);
						if (status != 0)
						{
							// Block not found
						}
						else
						{
							type_a = nano::block_type::state;
						}
					}
					else
					{
						type_a = nano::block_type::state;
					}
				}
				else
				{
					type_a = nano::block_type::change;
				}
			}
			else
			{
				type_a = nano::block_type::open;
			}
		}
		else
		{
			type_a = nano::block_type::receive;
		}
	}
	else
	{
		type_a = nano::block_type::send;
	}
	return result;
}

template <typename T>
std::shared_ptr<nano::block> nano::mdb_store::block_random (nano::transaction const & transaction_a, MDB_dbi database)
{
	nano::block_hash hash;
	nano::random_pool.GenerateBlock (hash.bytes.data (), hash.bytes.size ());
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
	auto region (nano::random_pool.GenerateWord32 (0, count.sum () - 1));
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

nano::block_hash nano::mdb_store::block_successor (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	nano::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	nano::block_hash result;
	if (value.mv_size != 0)
	{
		assert (value.mv_size >= result.bytes.size ());
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data) + value.mv_size - result.bytes.size (), result.bytes.size ());
		auto error (nano::read (stream, result.bytes));
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
	auto block (block_get (transaction_a, hash_a));
	auto version (block_version (transaction_a, hash_a));
	block_put (transaction_a, hash_a, *block, 0, version);
}

std::shared_ptr<nano::block> nano::mdb_store::block_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	nano::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	std::shared_ptr<nano::block> result;
	if (value.mv_size != 0)
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
		result = nano::deserialize_block (stream, type);
		assert (result != nullptr);
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
	// clang-format off
	return
		block_exists (tx_a, nano::block_type::send, hash_a) ||
		block_exists (tx_a, nano::block_type::receive, hash_a) ||
		block_exists (tx_a, nano::block_type::open, hash_a) ||
		block_exists (tx_a, nano::block_type::change, hash_a) ||
		block_exists (tx_a, nano::block_type::state, hash_a);
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
		info_a.deserialize (stream);
	}
	return result;
}

void nano::mdb_store::frontier_put (nano::transaction const & transaction_a, nano::block_hash const & block_a, nano::account const & account_a)
{
	auto status (mdb_put (env.tx (transaction_a), frontiers, nano::mdb_val (block_a), nano::mdb_val (account_a), 0));
	release_assert (status == 0);
}

nano::account nano::mdb_store::frontier_get (nano::transaction const & transaction_a, nano::block_hash const & block_a)
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

void nano::mdb_store::account_put (nano::transaction const & transaction_a, nano::account const & account_a, nano::account_info const & info_a)
{
	MDB_dbi db;
	switch (info_a.epoch)
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
	auto status (mdb_put (env.tx (transaction_a), db, nano::mdb_val (account_a), nano::mdb_val (info_a), 0));
	release_assert (status == 0);
}

void nano::mdb_store::pending_put (nano::transaction const & transaction_a, nano::pending_key const & key_a, nano::pending_info const & pending_a)
{
	MDB_dbi db;
	switch (pending_a.epoch)
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
	auto status (mdb_put (env.tx (transaction_a), db, nano::mdb_val (key_a), nano::mdb_val (pending_a), 0));
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
		pending_a.deserialize (stream);
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

void nano::mdb_store::block_info_put (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_info const & block_info_a)
{
	auto status (mdb_put (env.tx (transaction_a), blocks_info, nano::mdb_val (hash_a), nano::mdb_val (block_info_a), 0));
	release_assert (status == 0);
}

void nano::mdb_store::block_info_del (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto status (mdb_del (env.tx (transaction_a), blocks_info, nano::mdb_val (hash_a), nullptr));
	release_assert (status == 0);
}

bool nano::mdb_store::block_info_exists (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto iterator (block_info_begin (transaction_a, hash_a));
	return iterator != block_info_end () && nano::block_hash (iterator->first) == hash_a;
}

bool nano::mdb_store::block_info_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_info & block_info_a)
{
	nano::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), blocks_info, nano::mdb_val (hash_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	bool result (true);
	if (status != MDB_NOTFOUND)
	{
		result = false;
		assert (value.size () == sizeof (block_info_a.account.bytes) + sizeof (block_info_a.balance.bytes));
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error1 (nano::read (stream, block_info_a.account));
		assert (!error1);
		auto error2 (nano::read (stream, block_info_a.balance));
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
		auto error (nano::read (stream, rep));
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

void nano::mdb_store::unchecked_put (nano::transaction const & transaction_a, nano::unchecked_key const & key_a, std::shared_ptr<nano::block> const & block_a)
{
	mdb_val block (block_a);
	auto status (mdb_put (env.tx (transaction_a), unchecked, nano::mdb_val (key_a), block, 0));
	release_assert (status == 0);
}

void nano::mdb_store::unchecked_put (nano::transaction const & transaction_a, nano::block_hash const & hash_a, std::shared_ptr<nano::block> const & block_a)
{
	nano::unchecked_key key (hash_a, block_a->hash ());
	unchecked_put (transaction_a, key, block_a);
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

std::vector<std::shared_ptr<nano::block>> nano::mdb_store::unchecked_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	std::vector<std::shared_ptr<nano::block>> result;
	for (auto i (unchecked_begin (transaction_a, nano::unchecked_key (hash_a, 0))), n (unchecked_end ()); i != n && nano::block_hash (i->first.key ()) == hash_a; ++i)
	{
		std::shared_ptr<nano::block> block (i->second);
		result.push_back (block);
	}
	return result;
}

bool nano::mdb_store::unchecked_exists (nano::transaction const & transaction_a, nano::unchecked_key const & key_a)
{
	auto iterator (unchecked_begin (transaction_a, key_a));
	return iterator != unchecked_end () && nano::unchecked_key (iterator->first) == key_a;
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

void nano::mdb_store::checksum_put (nano::transaction const & transaction_a, uint64_t prefix, uint8_t mask, nano::uint256_union const & hash_a)
{
	assert ((prefix & 0xff) == 0);
	uint64_t key (prefix | mask);
	auto status (mdb_put (env.tx (transaction_a), checksum, nano::mdb_val (sizeof (key), &key), nano::mdb_val (hash_a), 0));
	release_assert (status == 0);
}

bool nano::mdb_store::checksum_get (nano::transaction const & transaction_a, uint64_t prefix, uint8_t mask, nano::uint256_union & hash_a)
{
	assert ((prefix & 0xff) == 0);
	uint64_t key (prefix | mask);
	nano::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), checksum, nano::mdb_val (sizeof (key), &key), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	bool result (true);
	if (status == 0)
	{
		result = false;
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error (nano::read (stream, hash_a));
		assert (!error);
	}
	return result;
}

void nano::mdb_store::checksum_del (nano::transaction const & transaction_a, uint64_t prefix, uint8_t mask)
{
	assert ((prefix & 0xff) == 0);
	uint64_t key (prefix | mask);
	auto status (mdb_del (env.tx (transaction_a), checksum, nano::mdb_val (sizeof (key), &key), nullptr));
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
