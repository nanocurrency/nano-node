
#include <rai/node/lmdb.hpp>

rai::mdb_env::mdb_env (bool & error_a, boost::filesystem::path const & path_a, int max_dbs)
{
	boost::system::error_code error;
	if (path_a.has_parent_path ())
	{
		boost::filesystem::create_directories (path_a.parent_path (), error);
		if (!error)
		{
			auto status1 (mdb_env_create (&environment));
			assert (status1 == 0);
			auto status2 (mdb_env_set_maxdbs (environment, max_dbs));
			assert (status2 == 0);
			auto status3 (mdb_env_set_mapsize (environment, 1ULL * 1024 * 1024 * 1024 * 128)); // 128 Gigabyte
			assert (status3 == 0);
			// It seems if there's ever more threads than mdb_env_set_maxreaders has read slots available, we get failures on transaction creation unless MDB_NOTLS is specified
			// This can happen if something like 256 io_threads are specified in the node config
			auto status4 (mdb_env_open (environment, path_a.string ().c_str (), MDB_NOSUBDIR | MDB_NOTLS, 00600));
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

rai::mdb_env::~mdb_env ()
{
	if (environment != nullptr)
	{
		mdb_env_close (environment);
	}
}

rai::mdb_env::operator MDB_env * () const
{
	return environment;
}

rai::mdb_val::mdb_val (rai::epoch epoch_a) :
value ({ 0, nullptr }),
epoch (epoch_a)
{
}

rai::mdb_val::mdb_val (MDB_val const & value_a, rai::epoch epoch_a) :
value (value_a),
epoch (epoch_a)
{
}

rai::mdb_val::mdb_val (size_t size_a, void * data_a) :
value ({ size_a, data_a })
{
}

rai::mdb_val::mdb_val (rai::uint128_union const & val_a) :
mdb_val (sizeof (val_a), const_cast<rai::uint128_union *> (&val_a))
{
}

rai::mdb_val::mdb_val (rai::uint256_union const & val_a) :
mdb_val (sizeof (val_a), const_cast<rai::uint256_union *> (&val_a))
{
}

rai::mdb_val::mdb_val (rai::account_info const & val_a) :
mdb_val (val_a.db_size (), const_cast<rai::account_info *> (&val_a))
{
}

rai::mdb_val::mdb_val (rai::pending_info const & val_a) :
mdb_val (sizeof (val_a.source) + sizeof (val_a.amount), const_cast<rai::pending_info *> (&val_a))
{
}

rai::mdb_val::mdb_val (rai::pending_key const & val_a) :
mdb_val (sizeof (val_a), const_cast<rai::pending_key *> (&val_a))
{
}

rai::mdb_val::mdb_val (rai::block_info const & val_a) :
mdb_val (sizeof (val_a), const_cast<rai::block_info *> (&val_a))
{
}

rai::mdb_val::mdb_val (std::shared_ptr<rai::block> const & val_a) :
buffer (std::make_shared<std::vector<uint8_t>> ())
{
	{
		rai::vectorstream stream (*buffer);
		rai::serialize_block (stream, *val_a);
	}
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}

void * rai::mdb_val::data () const
{
	return value.mv_data;
}

size_t rai::mdb_val::size () const
{
	return value.mv_size;
}

rai::mdb_val::operator rai::account_info () const
{
	rai::account_info result;
	result.epoch = epoch;
	assert (value.mv_size == result.db_size ());
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

rai::mdb_val::operator rai::block_info () const
{
	rai::block_info result;
	assert (value.mv_size == sizeof (result));
	static_assert (sizeof (rai::block_info::account) + sizeof (rai::block_info::balance) == sizeof (result), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}

rai::mdb_val::operator rai::pending_info () const
{
	rai::pending_info result;
	result.epoch = epoch;
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (rai::pending_info::source) + sizeof (rai::pending_info::amount), reinterpret_cast<uint8_t *> (&result));
	return result;
}

rai::mdb_val::operator rai::pending_key () const
{
	rai::pending_key result;
	assert (value.mv_size == sizeof (result));
	static_assert (sizeof (rai::pending_key::account) + sizeof (rai::pending_key::hash) == sizeof (result), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}

rai::mdb_val::operator rai::uint128_union () const
{
	rai::uint128_union result;
	assert (size () == sizeof (result));
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
	return result;
}

rai::mdb_val::operator rai::uint256_union () const
{
	rai::uint256_union result;
	assert (size () == sizeof (result));
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
	return result;
}

rai::mdb_val::operator std::array<char, 64> () const
{
	rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	std::array<char, 64> result;
	rai::read (stream, result);
	return result;
}

rai::mdb_val::operator no_value () const
{
	return no_value::dummy;
}

rai::mdb_val::operator std::shared_ptr<rai::block> () const
{
	rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	std::shared_ptr<rai::block> result (rai::deserialize_block (stream));
	return result;
}

rai::mdb_val::operator std::shared_ptr<rai::send_block> () const
{
	rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<rai::send_block> result (std::make_shared<rai::send_block> (error, stream));
	assert (!error);
	return result;
}

rai::mdb_val::operator std::shared_ptr<rai::receive_block> () const
{
	rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<rai::receive_block> result (std::make_shared<rai::receive_block> (error, stream));
	assert (!error);
	return result;
}

rai::mdb_val::operator std::shared_ptr<rai::open_block> () const
{
	rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<rai::open_block> result (std::make_shared<rai::open_block> (error, stream));
	assert (!error);
	return result;
}

rai::mdb_val::operator std::shared_ptr<rai::change_block> () const
{
	rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<rai::change_block> result (std::make_shared<rai::change_block> (error, stream));
	assert (!error);
	return result;
}

rai::mdb_val::operator std::shared_ptr<rai::state_block> () const
{
	rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<rai::state_block> result (std::make_shared<rai::state_block> (error, stream));
	assert (!error);
	return result;
}

rai::mdb_val::operator std::shared_ptr<rai::vote> () const
{
	auto result (std::make_shared<rai::vote> ());
	rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (rai::read (stream, result->account.bytes));
	assert (!error);
	error = rai::read (stream, result->signature.bytes);
	assert (!error);
	error = rai::read (stream, result->sequence);
	assert (!error);
	result->blocks.push_back (rai::deserialize_block (stream));
	assert (boost::get<std::shared_ptr<rai::block>> (result->blocks[0]) != nullptr);
	return result;
}

rai::mdb_val::operator uint64_t () const
{
	uint64_t result;
	rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (rai::read (stream, result));
	assert (!error);
	return result;
}

rai::mdb_val::operator MDB_val * () const
{
	// Allow passing a temporary to a non-c++ function which doesn't have constness
	return const_cast<MDB_val *> (&value);
};

rai::mdb_val::operator MDB_val const & () const
{
	return value;
}

rai::transaction::transaction (rai::mdb_env & environment_a, MDB_txn * parent_a, bool write) :
environment (environment_a)
{
	auto status (mdb_txn_begin (environment_a, parent_a, write ? 0 : MDB_RDONLY, &handle));
	assert (status == 0);
}

rai::transaction::~transaction ()
{
	auto status (mdb_txn_commit (handle));
	assert (status == 0);
}

rai::transaction::operator MDB_txn * () const
{
	return handle;
}
