#include <queue>
#include <rai/node/common.hpp>
#include <rai/node/wallet.hpp>
#include <rai/secure/blockstore.hpp>
#include <rai/secure/versioning.hpp>

#include <boost/polymorphic_cast.hpp>

namespace rai
{
/**
 * Fill in our predecessors
 */
class block_predecessor_set : public rai::block_visitor
{
public:
	block_predecessor_set (MDB_txn * transaction_a, rai::block_store & store_a) :
	transaction (transaction_a),
	store (store_a)
	{
	}
	virtual ~block_predecessor_set () = default;
	void fill_value (rai::block const & block_a)
	{
		auto hash (block_a.hash ());
		rai::block_type type;
		auto value (store.block_raw_get (transaction, block_a.previous (), type));
		auto version (store.block_version (transaction, block_a.previous ()));
		assert (value.mv_size != 0);
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.mv_data), static_cast<uint8_t *> (value.mv_data) + value.mv_size);
		std::copy (hash.bytes.begin (), hash.bytes.end (), data.end () - hash.bytes.size ());
		store.block_raw_put (transaction, store.block_database (type, version), block_a.previous (), rai::mdb_val (data.size (), data.data ()));
	}
	void send_block (rai::send_block const & block_a) override
	{
		fill_value (block_a);
	}
	void receive_block (rai::receive_block const & block_a) override
	{
		fill_value (block_a);
	}
	void open_block (rai::open_block const & block_a) override
	{
		// Open blocks don't have a predecessor
	}
	void change_block (rai::change_block const & block_a) override
	{
		fill_value (block_a);
	}
	void state_block (rai::state_block const & block_a) override
	{
		if (!block_a.previous ().is_zero ())
		{
			fill_value (block_a);
		}
	}
	MDB_txn * transaction;
	rai::block_store & store;
};
}

template <typename T, typename U>
rai::mdb_iterator<T, U>::mdb_iterator (MDB_txn * transaction_a, MDB_dbi db_a, rai::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
	auto status (mdb_cursor_open (transaction_a, db_a, &cursor));
	assert (status == 0);
	auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_FIRST));
	assert (status2 == 0 || status2 == MDB_NOTFOUND);
	if (status2 != MDB_NOTFOUND)
	{
		auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
		assert (status3 == 0 || status3 == MDB_NOTFOUND);
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
rai::mdb_iterator<T, U>::mdb_iterator (std::nullptr_t, rai::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
}

template <typename T, typename U>
rai::mdb_iterator<T, U>::mdb_iterator (MDB_txn * transaction_a, MDB_dbi db_a, MDB_val const & val_a, rai::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
	auto status (mdb_cursor_open (transaction_a, db_a, &cursor));
	assert (status == 0);
	current.first = val_a;
	auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_SET_RANGE));
	assert (status2 == 0 || status2 == MDB_NOTFOUND);
	if (status2 != MDB_NOTFOUND)
	{
		auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
		assert (status3 == 0 || status3 == MDB_NOTFOUND);
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
rai::mdb_iterator<T, U>::mdb_iterator (rai::mdb_iterator<T, U> && other_a)
{
	cursor = other_a.cursor;
	other_a.cursor = nullptr;
	current = other_a.current;
}

template <typename T, typename U>
rai::mdb_iterator<T, U>::~mdb_iterator ()
{
	if (cursor != nullptr)
	{
		mdb_cursor_close (cursor);
	}
}

template <typename T, typename U>
rai::store_iterator_impl<T, U> & rai::mdb_iterator<T, U>::operator++ ()
{
	assert (cursor != nullptr);
	auto status (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_NEXT));
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
rai::mdb_iterator<T, U> & rai::mdb_iterator<T, U>::operator= (rai::mdb_iterator<T, U> && other_a)
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
std::pair<rai::mdb_val, rai::mdb_val> * rai::mdb_iterator<T, U>::operator-> ()
{
	return &current;
}

template <typename T, typename U>
bool rai::mdb_iterator<T, U>::operator== (rai::store_iterator_impl<T, U> const & base_a) const
{
	auto const other_a (boost::polymorphic_downcast<rai::mdb_iterator<T, U> const *> (&base_a));
	auto result (current.first.data () == other_a->current.first.data ());
	assert (!result || (current.first.size () == other_a->current.first.size ()));
	assert (!result || (current.second.data () == other_a->current.second.data ()));
	assert (!result || (current.second.size () == other_a->current.second.size ()));
	return result;
}

template <typename T, typename U>
void rai::mdb_iterator<T, U>::next_dup ()
{
	assert (cursor != nullptr);
	auto status (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_NEXT_DUP));
	if (status == MDB_NOTFOUND)
	{
		clear ();
	}
}

template <typename T, typename U>
void rai::mdb_iterator<T, U>::clear ()
{
	current.first = rai::mdb_val (current.first.epoch);
	current.second = rai::mdb_val (current.second.epoch);
	assert (is_end_sentinal ());
}

template <typename T, typename U>
bool rai::mdb_iterator<T, U>::is_end_sentinal () const
{
	return current.first.size () == 0;
}

template <typename T, typename U>
void rai::mdb_iterator<T, U>::fill (std::pair<T, U> & value_a) const
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
std::pair<rai::mdb_val, rai::mdb_val> * rai::mdb_merge_iterator<T, U>::operator-> ()
{
	return least_iterator ().operator-> ();
}

template <typename T, typename U>
rai::mdb_merge_iterator<T, U>::mdb_merge_iterator (MDB_txn * transaction_a, MDB_dbi db1_a, MDB_dbi db2_a) :
impl1 (std::make_unique<rai::mdb_iterator<T, U>> (transaction_a, db1_a, rai::epoch::epoch_0)),
impl2 (std::make_unique<rai::mdb_iterator<T, U>> (transaction_a, db2_a, rai::epoch::epoch_1))
{
}

template <typename T, typename U>
rai::mdb_merge_iterator<T, U>::mdb_merge_iterator (std::nullptr_t) :
impl1 (std::make_unique<rai::mdb_iterator<T, U>> (nullptr, rai::epoch::epoch_0)),
impl2 (std::make_unique<rai::mdb_iterator<T, U>> (nullptr, rai::epoch::epoch_1))
{
}

template <typename T, typename U>
rai::mdb_merge_iterator<T, U>::mdb_merge_iterator (MDB_txn * transaction_a, MDB_dbi db1_a, MDB_dbi db2_a, MDB_val const & val_a) :
impl1 (std::make_unique<rai::mdb_iterator<T, U>> (transaction_a, db1_a, val_a, rai::epoch::epoch_0)),
impl2 (std::make_unique<rai::mdb_iterator<T, U>> (transaction_a, db2_a, val_a, rai::epoch::epoch_1))
{
}

template <typename T, typename U>
rai::mdb_merge_iterator<T, U>::mdb_merge_iterator (rai::mdb_merge_iterator<T, U> && other_a)
{
	impl1 = std::move (other_a.impl1);
	impl2 = std::move (other_a.impl2);
}

template <typename T, typename U>
rai::mdb_merge_iterator<T, U>::~mdb_merge_iterator ()
{
}

template <typename T, typename U>
rai::store_iterator_impl<T, U> & rai::mdb_merge_iterator<T, U>::operator++ ()
{
	++least_iterator ();
	return *this;
}

template <typename T, typename U>
void rai::mdb_merge_iterator<T, U>::next_dup ()
{
	least_iterator ().next_dup ();
}

template <typename T, typename U>
bool rai::mdb_merge_iterator<T, U>::is_end_sentinal () const
{
	return least_iterator ().is_end_sentinal ();
}

template <typename T, typename U>
void rai::mdb_merge_iterator<T, U>::fill (std::pair<T, U> & value_a) const
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
bool rai::mdb_merge_iterator<T, U>::operator== (rai::store_iterator_impl<T, U> const & base_a) const
{
	assert ((dynamic_cast<rai::mdb_merge_iterator<T, U> const *> (&base_a) != nullptr) && "Incompatible iterator comparison");
	auto & other (static_cast<rai::mdb_merge_iterator<T, U> const &> (base_a));
	return *impl1 == *other.impl1 && *impl2 == *other.impl2;
}

template <typename T, typename U>
rai::mdb_iterator<T, U> & rai::mdb_merge_iterator<T, U>::least_iterator () const
{
	rai::mdb_iterator<T, U> * result;
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
		auto val_cmp (mdb_cmp (mdb_cursor_txn (impl1->cursor), mdb_cursor_dbi (impl1->cursor), impl1->current.second, impl2->current.second));
		result = key_cmp < 0 ? impl1.get () : val_cmp < 0 ? impl1.get () : impl2.get ();
	}
	return *result;
}

template class rai::mdb_iterator<rai::pending_key, rai::pending_info>;
template class rai::mdb_iterator<rai::uint256_union, rai::block_info>;
template class rai::mdb_iterator<rai::uint256_union, rai::uint128_union>;
template class rai::mdb_iterator<rai::uint256_union, rai::uint256_union>;
template class rai::mdb_iterator<rai::uint256_union, std::shared_ptr<rai::block>>;
template class rai::mdb_iterator<rai::uint256_union, std::shared_ptr<rai::vote>>;
template class rai::mdb_iterator<rai::uint256_union, rai::wallet_value>;
template class rai::mdb_iterator<std::array<char, 64>, rai::mdb_val::no_value>;

rai::store_iterator<rai::block_hash, rai::block_info> rai::block_store::block_info_begin (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	rai::store_iterator<rai::block_hash, rai::block_info> result (std::make_unique<rai::mdb_iterator<rai::block_hash, rai::block_info>> (transaction_a, blocks_info, rai::mdb_val (hash_a)));
	return result;
}

rai::store_iterator<rai::block_hash, rai::block_info> rai::block_store::block_info_begin (MDB_txn * transaction_a)
{
	rai::store_iterator<rai::block_hash, rai::block_info> result (std::make_unique<rai::mdb_iterator<rai::block_hash, rai::block_info>> (transaction_a, blocks_info));
	return result;
}

rai::store_iterator<rai::block_hash, rai::block_info> rai::block_store::block_info_end ()
{
	rai::store_iterator<rai::block_hash, rai::block_info> result (nullptr);
	return result;
}

rai::store_iterator<rai::account, rai::uint128_union> rai::block_store::representation_begin (MDB_txn * transaction_a)
{
	rai::store_iterator<rai::account, rai::uint128_union> result (std::make_unique<rai::mdb_iterator<rai::account, rai::uint128_union>> (transaction_a, representation));
	return result;
}

rai::store_iterator<rai::account, rai::uint128_union> rai::block_store::representation_end ()
{
	rai::store_iterator<rai::account, rai::uint128_union> result (nullptr);
	return result;
}

rai::store_iterator<rai::block_hash, std::shared_ptr<rai::block>> rai::block_store::unchecked_begin (MDB_txn * transaction_a)
{
	rai::store_iterator<rai::block_hash, std::shared_ptr<rai::block>> result (std::make_unique<rai::mdb_iterator<rai::account, std::shared_ptr<rai::block>>> (transaction_a, unchecked));
	return result;
}

rai::store_iterator<rai::block_hash, std::shared_ptr<rai::block>> rai::block_store::unchecked_begin (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	rai::store_iterator<rai::block_hash, std::shared_ptr<rai::block>> result (std::make_unique<rai::mdb_iterator<rai::block_hash, std::shared_ptr<rai::block>>> (transaction_a, unchecked, rai::mdb_val (hash_a)));
	return result;
}

rai::store_iterator<rai::block_hash, std::shared_ptr<rai::block>> rai::block_store::unchecked_end ()
{
	rai::store_iterator<rai::block_hash, std::shared_ptr<rai::block>> result (nullptr);
	return result;
}

rai::store_iterator<rai::account, std::shared_ptr<rai::vote>> rai::block_store::vote_begin (MDB_txn * transaction_a)
{
	return rai::store_iterator<rai::account, std::shared_ptr<rai::vote>> (std::make_unique<rai::mdb_iterator<rai::account, std::shared_ptr<rai::vote>>> (transaction_a, vote));
}

rai::store_iterator<rai::account, std::shared_ptr<rai::vote>> rai::block_store::vote_end ()
{
	return rai::store_iterator<rai::account, std::shared_ptr<rai::vote>> (nullptr);
}

rai::block_store::block_store (bool & error_a, boost::filesystem::path const & path_a, int lmdb_max_dbs) :
environment (error_a, path_a, lmdb_max_dbs),
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
		rai::transaction transaction (environment, nullptr, true);
		error_a |= mdb_dbi_open (transaction, "frontiers", MDB_CREATE, &frontiers) != 0;
		error_a |= mdb_dbi_open (transaction, "accounts", MDB_CREATE, &accounts_v0) != 0;
		error_a |= mdb_dbi_open (transaction, "accounts_v1", MDB_CREATE, &accounts_v1) != 0;
		error_a |= mdb_dbi_open (transaction, "send", MDB_CREATE, &send_blocks) != 0;
		error_a |= mdb_dbi_open (transaction, "receive", MDB_CREATE, &receive_blocks) != 0;
		error_a |= mdb_dbi_open (transaction, "open", MDB_CREATE, &open_blocks) != 0;
		error_a |= mdb_dbi_open (transaction, "change", MDB_CREATE, &change_blocks) != 0;
		error_a |= mdb_dbi_open (transaction, "state", MDB_CREATE, &state_blocks_v0) != 0;
		error_a |= mdb_dbi_open (transaction, "state_v1", MDB_CREATE, &state_blocks_v1) != 0;
		error_a |= mdb_dbi_open (transaction, "pending", MDB_CREATE, &pending_v0) != 0;
		error_a |= mdb_dbi_open (transaction, "pending_v1", MDB_CREATE, &pending_v1) != 0;
		error_a |= mdb_dbi_open (transaction, "blocks_info", MDB_CREATE, &blocks_info) != 0;
		error_a |= mdb_dbi_open (transaction, "representation", MDB_CREATE, &representation) != 0;
		error_a |= mdb_dbi_open (transaction, "unchecked", MDB_CREATE | MDB_DUPSORT, &unchecked) != 0;
		error_a |= mdb_dbi_open (transaction, "checksum", MDB_CREATE, &checksum) != 0;
		error_a |= mdb_dbi_open (transaction, "vote", MDB_CREATE, &vote) != 0;
		error_a |= mdb_dbi_open (transaction, "meta", MDB_CREATE, &meta) != 0;
		if (!error_a)
		{
			do_upgrades (transaction);
			checksum_put (transaction, 0, 0, 0);
		}
	}
}

void rai::block_store::initialize (MDB_txn * transaction_a, rai::genesis const & genesis_a)
{
	auto hash_l (genesis_a.hash ());
	assert (latest_v0_begin (transaction_a) == latest_v0_end ());
	assert (latest_v1_begin (transaction_a) == latest_v1_end ());
	block_put (transaction_a, hash_l, *genesis_a.open);
	account_put (transaction_a, genesis_account, { hash_l, genesis_a.open->hash (), genesis_a.open->hash (), std::numeric_limits<rai::uint128_t>::max (), rai::seconds_since_epoch (), 1, rai::epoch::epoch_0 });
	representation_put (transaction_a, genesis_account, std::numeric_limits<rai::uint128_t>::max ());
	checksum_put (transaction_a, 0, 0, hash_l);
	frontier_put (transaction_a, hash_l, genesis_account);
}

void rai::block_store::version_put (MDB_txn * transaction_a, int version_a)
{
	rai::uint256_union version_key (1);
	rai::uint256_union version_value (version_a);
	auto status (mdb_put (transaction_a, meta, rai::mdb_val (version_key), rai::mdb_val (version_value), 0));
	assert (status == 0);
}

int rai::block_store::version_get (MDB_txn * transaction_a)
{
	rai::uint256_union version_key (1);
	rai::mdb_val data;
	auto error (mdb_get (transaction_a, meta, rai::mdb_val (version_key), data));
	int result;
	if (error == MDB_NOTFOUND)
	{
		result = 1;
	}
	else
	{
		rai::uint256_union version_value (data);
		assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
		result = version_value.number ().convert_to<int> ();
	}
	return result;
}

rai::raw_key rai::block_store::get_node_id (MDB_txn * transaction_a)
{
	rai::uint256_union node_id_mdb_key (3);
	rai::raw_key node_id;
	rai::mdb_val value;
	auto error (mdb_get (transaction_a, meta, rai::mdb_val (node_id_mdb_key), value));
	if (!error)
	{
		rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		error = rai::read (stream, node_id.data);
		assert (!error);
	}
	if (error)
	{
		rai::random_pool.GenerateBlock (node_id.data.bytes.data (), node_id.data.bytes.size ());
		error = mdb_put (transaction_a, meta, rai::mdb_val (node_id_mdb_key), rai::mdb_val (node_id.data), 0);
	}
	assert (!error);
	return node_id;
}

void rai::block_store::delete_node_id (MDB_txn * transaction_a)
{
	rai::uint256_union node_id_mdb_key (3);
	auto error (mdb_del (transaction_a, meta, rai::mdb_val (node_id_mdb_key), nullptr));
	assert (!error || error == MDB_NOTFOUND);
}

void rai::block_store::do_upgrades (MDB_txn * transaction_a)
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
			break;
		default:
			assert (false);
	}
}

void rai::block_store::upgrade_v1_to_v2 (MDB_txn * transaction_a)
{
	version_put (transaction_a, 2);
	rai::account account (1);
	while (!account.is_zero ())
	{
		rai::mdb_iterator<rai::uint256_union, rai::account_info_v1> i (transaction_a, accounts_v0, rai::mdb_val (account));
		std::cerr << std::hex;
		if (i != rai::mdb_iterator<rai::uint256_union, rai::account_info_v1> (nullptr))
		{
			account = rai::uint256_union (i->first);
			rai::account_info_v1 v1 (i->second);
			rai::account_info_v5 v2;
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
			auto status (mdb_put (transaction_a, accounts_v0, rai::mdb_val (account), v2.val (), 0));
			assert (status == 0);
			account = account.number () + 1;
		}
		else
		{
			account.clear ();
		}
	}
}

void rai::block_store::upgrade_v2_to_v3 (MDB_txn * transaction_a)
{
	version_put (transaction_a, 3);
	mdb_drop (transaction_a, representation, 0);
	for (auto i (rai::store_iterator<rai::account, rai::account_info_v5> (std::make_unique<rai::mdb_iterator<rai::account, rai::account_info_v5>> (transaction_a, accounts_v0))), n (rai::store_iterator<rai::account, rai::account_info_v5> (nullptr)); i != n; ++i)
	{
		rai::account account_l (i->first);
		rai::account_info_v5 info (i->second);
		representative_visitor visitor (transaction_a, *this);
		visitor.compute (info.head);
		assert (!visitor.result.is_zero ());
		info.rep_block = visitor.result;
		auto impl (boost::polymorphic_downcast<rai::mdb_iterator<rai::account, rai::account_info_v5> *> (i.impl.get ()));
		mdb_cursor_put (impl->cursor, rai::mdb_val (account_l), info.val (), MDB_CURRENT);
		representation_add (transaction_a, visitor.result, info.balance.number ());
	}
}

void rai::block_store::upgrade_v3_to_v4 (MDB_txn * transaction_a)
{
	version_put (transaction_a, 4);
	std::queue<std::pair<rai::pending_key, rai::pending_info>> items;
	for (auto i (rai::store_iterator<rai::block_hash, rai::pending_info_v3> (std::make_unique<rai::mdb_iterator<rai::block_hash, rai::pending_info_v3>> (transaction_a, pending_v0))), n (rai::store_iterator<rai::block_hash, rai::pending_info_v3> (nullptr)); i != n; ++i)
	{
		rai::block_hash hash (i->first);
		rai::pending_info_v3 info (i->second);
		items.push (std::make_pair (rai::pending_key (info.destination, hash), rai::pending_info (info.source, info.amount, rai::epoch::epoch_0)));
	}
	mdb_drop (transaction_a, pending_v0, 0);
	while (!items.empty ())
	{
		pending_put (transaction_a, items.front ().first, items.front ().second);
		items.pop ();
	}
}

void rai::block_store::upgrade_v4_to_v5 (MDB_txn * transaction_a)
{
	version_put (transaction_a, 5);
	for (auto i (rai::store_iterator<rai::account, rai::account_info_v5> (std::make_unique<rai::mdb_iterator<rai::account, rai::account_info_v5>> (transaction_a, accounts_v0))), n (rai::store_iterator<rai::account, rai::account_info_v5> (nullptr)); i != n; ++i)
	{
		rai::account_info_v5 info (i->second);
		rai::block_hash successor (0);
		auto block (block_get (transaction_a, info.head));
		while (block != nullptr)
		{
			auto hash (block->hash ());
			if (block_successor (transaction_a, hash).is_zero () && !successor.is_zero ())
			{
				//std::cerr << boost::str (boost::format ("Adding successor for account %1%, block %2%, successor %3%\n") % account.to_account () % hash.to_string () % successor.to_string ());
				block_put (transaction_a, hash, *block, successor);
			}
			successor = hash;
			block = block_get (transaction_a, block->previous ());
		}
	}
}

void rai::block_store::upgrade_v5_to_v6 (MDB_txn * transaction_a)
{
	version_put (transaction_a, 6);
	std::deque<std::pair<rai::account, rai::account_info>> headers;
	for (auto i (rai::store_iterator<rai::account, rai::account_info_v5> (std::make_unique<rai::mdb_iterator<rai::account, rai::account_info_v5>> (transaction_a, accounts_v0))), n (rai::store_iterator<rai::account, rai::account_info_v5> (nullptr)); i != n; ++i)
	{
		rai::account account (i->first);
		rai::account_info_v5 info_old (i->second);
		uint64_t block_count (0);
		auto hash (info_old.head);
		while (!hash.is_zero ())
		{
			++block_count;
			auto block (block_get (transaction_a, hash));
			assert (block != nullptr);
			hash = block->previous ();
		}
		rai::account_info info (info_old.head, info_old.rep_block, info_old.open_block, info_old.balance, info_old.modified, block_count, rai::epoch::epoch_0);
		headers.push_back (std::make_pair (account, info));
	}
	for (auto i (headers.begin ()), n (headers.end ()); i != n; ++i)
	{
		account_put (transaction_a, i->first, i->second);
	}
}

void rai::block_store::upgrade_v6_to_v7 (MDB_txn * transaction_a)
{
	version_put (transaction_a, 7);
	mdb_drop (transaction_a, unchecked, 0);
}

void rai::block_store::upgrade_v7_to_v8 (MDB_txn * transaction_a)
{
	version_put (transaction_a, 8);
	mdb_drop (transaction_a, unchecked, 1);
	mdb_dbi_open (transaction_a, "unchecked", MDB_CREATE | MDB_DUPSORT, &unchecked);
}

void rai::block_store::upgrade_v8_to_v9 (MDB_txn * transaction_a)
{
	version_put (transaction_a, 9);
	MDB_dbi sequence;
	mdb_dbi_open (transaction_a, "sequence", MDB_CREATE | MDB_DUPSORT, &sequence);
	rai::genesis genesis;
	std::shared_ptr<rai::block> block (std::move (genesis.open));
	rai::keypair junk;
	for (rai::mdb_iterator<rai::account, uint64_t> i (transaction_a, sequence), n (rai::mdb_iterator<rai::account, uint64_t> (nullptr)); i != n; ++i)
	{
		rai::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
		uint64_t sequence;
		auto error (rai::read (stream, sequence));
		// Create a dummy vote with the same sequence number for easy upgrading.  This won't have a valid signature.
		rai::vote dummy (rai::account (i->first), junk.prv, sequence, block);
		std::vector<uint8_t> vector;
		{
			rai::vectorstream stream (vector);
			dummy.serialize (stream);
		}
		auto status1 (mdb_put (transaction_a, vote, rai::mdb_val (i->first), rai::mdb_val (vector.size (), vector.data ()), 0));
		assert (status1 == 0);
		assert (!error);
	}
	mdb_drop (transaction_a, sequence, 1);
}

void rai::block_store::upgrade_v9_to_v10 (MDB_txn * transaction_a)
{
	//std::cerr << boost::str (boost::format ("Performing database upgrade to version 10...\n"));
	version_put (transaction_a, 10);
	for (auto i (latest_v0_begin (transaction_a)), n (latest_v0_end ()); i != n; ++i)
	{
		rai::account_info info (i->second);
		if (info.block_count >= block_info_max)
		{
			rai::account account (i->first);
			//std::cerr << boost::str (boost::format ("Upgrading account %1%...\n") % account.to_account ());
			size_t block_count (1);
			auto hash (info.open_block);
			while (!hash.is_zero ())
			{
				if ((block_count % block_info_max) == 0)
				{
					rai::block_info block_info;
					block_info.account = account;
					rai::amount balance (block_balance (transaction_a, hash));
					block_info.balance = balance;
					block_info_put (transaction_a, hash, block_info);
				}
				hash = block_successor (transaction_a, hash);
				++block_count;
			}
		}
	}
	//std::cerr << boost::str (boost::format ("Database upgrade is completed\n"));
}

void rai::block_store::upgrade_v10_to_v11 (MDB_txn * transaction_a)
{
	version_put (transaction_a, 11);
	MDB_dbi unsynced;
	mdb_dbi_open (transaction_a, "unsynced", MDB_CREATE | MDB_DUPSORT, &unsynced);
	mdb_drop (transaction_a, unsynced, 1);
}

void rai::block_store::clear (MDB_dbi db_a)
{
	rai::transaction transaction (environment, nullptr, true);
	auto status (mdb_drop (transaction, db_a, 0));
	assert (status == 0);
}

rai::uint128_t rai::block_store::block_balance (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	balance_visitor visitor (transaction_a, *this);
	visitor.compute (hash_a);
	return visitor.balance;
}

rai::epoch rai::block_store::block_version (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	rai::mdb_val value;
	auto status (mdb_get (transaction_a, state_blocks_v1, rai::mdb_val (hash_a), value));
	assert (status == 0 || status == MDB_NOTFOUND);
	return status == 0 ? rai::epoch::epoch_1 : rai::epoch::epoch_0;
}

void rai::block_store::representation_add (MDB_txn * transaction_a, rai::block_hash const & source_a, rai::uint128_t const & amount_a)
{
	auto source_block (block_get (transaction_a, source_a));
	assert (source_block != nullptr);
	auto source_rep (source_block->representative ());
	auto source_previous (representation_get (transaction_a, source_rep));
	representation_put (transaction_a, source_rep, source_previous + amount_a);
}

MDB_dbi rai::block_store::block_database (rai::block_type type_a, rai::epoch epoch_a)
{
	if (type_a == rai::block_type::state)
	{
		assert (epoch_a == rai::epoch::epoch_0 || epoch_a == rai::epoch::epoch_1);
	}
	else
	{
		assert (epoch_a == rai::epoch::epoch_0);
	}
	MDB_dbi result;
	switch (type_a)
	{
		case rai::block_type::send:
			result = send_blocks;
			break;
		case rai::block_type::receive:
			result = receive_blocks;
			break;
		case rai::block_type::open:
			result = open_blocks;
			break;
		case rai::block_type::change:
			result = change_blocks;
			break;
		case rai::block_type::state:
			switch (epoch_a)
			{
				case rai::epoch::epoch_0:
					result = state_blocks_v0;
					break;
				case rai::epoch::epoch_1:
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

void rai::block_store::block_raw_put (MDB_txn * transaction_a, MDB_dbi database_a, rai::block_hash const & hash_a, MDB_val value_a)
{
	auto status2 (mdb_put (transaction_a, database_a, rai::mdb_val (hash_a), &value_a, 0));
	assert (status2 == 0);
}

void rai::block_store::block_put (MDB_txn * transaction_a, rai::block_hash const & hash_a, rai::block const & block_a, rai::block_hash const & successor_a, rai::epoch epoch_a)
{
	assert (successor_a.is_zero () || block_exists (transaction_a, successor_a));
	std::vector<uint8_t> vector;
	{
		rai::vectorstream stream (vector);
		block_a.serialize (stream);
		rai::write (stream, successor_a.bytes);
	}
	block_raw_put (transaction_a, block_database (block_a.type (), epoch_a), hash_a, { vector.size (), vector.data () });
	rai::block_predecessor_set predecessor (transaction_a, *this);
	block_a.visit (predecessor);
	assert (block_a.previous ().is_zero () || block_successor (transaction_a, block_a.previous ()) == hash_a);
}

MDB_val rai::block_store::block_raw_get (MDB_txn * transaction_a, rai::block_hash const & hash_a, rai::block_type & type_a)
{
	rai::mdb_val result;
	auto status (mdb_get (transaction_a, send_blocks, rai::mdb_val (hash_a), result));
	assert (status == 0 || status == MDB_NOTFOUND);
	if (status != 0)
	{
		auto status (mdb_get (transaction_a, receive_blocks, rai::mdb_val (hash_a), result));
		assert (status == 0 || status == MDB_NOTFOUND);
		if (status != 0)
		{
			auto status (mdb_get (transaction_a, open_blocks, rai::mdb_val (hash_a), result));
			assert (status == 0 || status == MDB_NOTFOUND);
			if (status != 0)
			{
				auto status (mdb_get (transaction_a, change_blocks, rai::mdb_val (hash_a), result));
				assert (status == 0 || status == MDB_NOTFOUND);
				if (status != 0)
				{
					auto status (mdb_get (transaction_a, state_blocks_v0, rai::mdb_val (hash_a), result));
					assert (status == 0 || status == MDB_NOTFOUND);
					if (status != 0)
					{
						auto status (mdb_get (transaction_a, state_blocks_v1, rai::mdb_val (hash_a), result));
						assert (status == 0 || status == MDB_NOTFOUND);
						if (status != 0)
						{
							// Block not found
						}
						else
						{
							type_a = rai::block_type::state;
						}
					}
					else
					{
						type_a = rai::block_type::state;
					}
				}
				else
				{
					type_a = rai::block_type::change;
				}
			}
			else
			{
				type_a = rai::block_type::open;
			}
		}
		else
		{
			type_a = rai::block_type::receive;
		}
	}
	else
	{
		type_a = rai::block_type::send;
	}
	return result;
}

template <typename T>
std::unique_ptr<rai::block> rai::block_store::block_random (MDB_txn * transaction_a, MDB_dbi database)
{
	rai::block_hash hash;
	rai::random_pool.GenerateBlock (hash.bytes.data (), hash.bytes.size ());
	rai::store_iterator<rai::block_hash, std::shared_ptr<T>> existing (std::make_unique<rai::mdb_iterator<rai::block_hash, std::shared_ptr<T>>> (transaction_a, database, rai::mdb_val (hash)));
	if (existing == rai::store_iterator<rai::block_hash, std::shared_ptr<T>> (nullptr))
	{
		existing = rai::store_iterator<rai::block_hash, std::shared_ptr<T>> (std::make_unique<rai::mdb_iterator<rai::block_hash, std::shared_ptr<T>>> (transaction_a, database));
	}
	auto end (rai::store_iterator<rai::block_hash, std::shared_ptr<T>> (nullptr));
	assert (existing != end);
	return block_get (transaction_a, rai::block_hash (existing->first));
}

std::unique_ptr<rai::block> rai::block_store::block_random (MDB_txn * transaction_a)
{
	auto count (block_count (transaction_a));
	auto region (rai::random_pool.GenerateWord32 (0, count.sum () - 1));
	std::unique_ptr<rai::block> result;
	if (region < count.send)
	{
		result = block_random<rai::send_block> (transaction_a, send_blocks);
	}
	else
	{
		region -= count.send;
		if (region < count.receive)
		{
			result = block_random<rai::receive_block> (transaction_a, receive_blocks);
		}
		else
		{
			region -= count.receive;
			if (region < count.open)
			{
				result = block_random<rai::open_block> (transaction_a, open_blocks);
			}
			else
			{
				region -= count.open;
				if (region < count.change)
				{
					result = block_random<rai::change_block> (transaction_a, change_blocks);
				}
				else
				{
					region -= count.change;
					if (region < count.state_v0)
					{
						result = block_random<rai::state_block> (transaction_a, state_blocks_v0);
					}
					else
					{
						result = block_random<rai::state_block> (transaction_a, state_blocks_v1);
					}
				}
			}
		}
	}
	assert (result != nullptr);
	return result;
}

rai::block_hash rai::block_store::block_successor (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	rai::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	rai::block_hash result;
	if (value.mv_size != 0)
	{
		assert (value.mv_size >= result.bytes.size ());
		rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data) + value.mv_size - result.bytes.size (), result.bytes.size ());
		auto error (rai::read (stream, result.bytes));
		assert (!error);
	}
	else
	{
		result.clear ();
	}
	return result;
}

void rai::block_store::block_successor_clear (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	auto block (block_get (transaction_a, hash_a));
	auto version (block_version (transaction_a, hash_a));
	block_put (transaction_a, hash_a, *block, 0, version);
}

std::unique_ptr<rai::block> rai::block_store::block_get (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	rai::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	std::unique_ptr<rai::block> result;
	if (value.mv_size != 0)
	{
		rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
		result = rai::deserialize_block (stream, type);
		assert (result != nullptr);
	}
	return result;
}

void rai::block_store::block_del (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	auto status (mdb_del (transaction_a, state_blocks_v1, rai::mdb_val (hash_a), nullptr));
	assert (status == 0 || status == MDB_NOTFOUND);
	if (status != 0)
	{
		auto status (mdb_del (transaction_a, state_blocks_v0, rai::mdb_val (hash_a), nullptr));
		assert (status == 0 || status == MDB_NOTFOUND);
		if (status != 0)
		{
			auto status (mdb_del (transaction_a, send_blocks, rai::mdb_val (hash_a), nullptr));
			assert (status == 0 || status == MDB_NOTFOUND);
			if (status != 0)
			{
				auto status (mdb_del (transaction_a, receive_blocks, rai::mdb_val (hash_a), nullptr));
				assert (status == 0 || status == MDB_NOTFOUND);
				if (status != 0)
				{
					auto status (mdb_del (transaction_a, open_blocks, rai::mdb_val (hash_a), nullptr));
					assert (status == 0 || status == MDB_NOTFOUND);
					if (status != 0)
					{
						auto status (mdb_del (transaction_a, change_blocks, rai::mdb_val (hash_a), nullptr));
						assert (status == 0);
					}
				}
			}
		}
	}
}

bool rai::block_store::block_exists (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	auto exists (true);
	rai::mdb_val junk;
	auto status (mdb_get (transaction_a, send_blocks, rai::mdb_val (hash_a), junk));
	assert (status == 0 || status == MDB_NOTFOUND);
	exists = status == 0;
	if (!exists)
	{
		auto status (mdb_get (transaction_a, receive_blocks, rai::mdb_val (hash_a), junk));
		assert (status == 0 || status == MDB_NOTFOUND);
		exists = status == 0;
		if (!exists)
		{
			auto status (mdb_get (transaction_a, open_blocks, rai::mdb_val (hash_a), junk));
			assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			if (!exists)
			{
				auto status (mdb_get (transaction_a, change_blocks, rai::mdb_val (hash_a), junk));
				assert (status == 0 || status == MDB_NOTFOUND);
				exists = status == 0;
				if (!exists)
				{
					auto status (mdb_get (transaction_a, state_blocks_v0, rai::mdb_val (hash_a), junk));
					assert (status == 0 || status == MDB_NOTFOUND);
					exists = status == 0;
					if (!exists)
					{
						auto status (mdb_get (transaction_a, state_blocks_v1, rai::mdb_val (hash_a), junk));
						assert (status == 0 || status == MDB_NOTFOUND);
						exists = status == 0;
					}
				}
			}
		}
	}
	return exists;
}

rai::block_counts rai::block_store::block_count (MDB_txn * transaction_a)
{
	rai::block_counts result;
	MDB_stat send_stats;
	auto status1 (mdb_stat (transaction_a, send_blocks, &send_stats));
	assert (status1 == 0);
	MDB_stat receive_stats;
	auto status2 (mdb_stat (transaction_a, receive_blocks, &receive_stats));
	assert (status2 == 0);
	MDB_stat open_stats;
	auto status3 (mdb_stat (transaction_a, open_blocks, &open_stats));
	assert (status3 == 0);
	MDB_stat change_stats;
	auto status4 (mdb_stat (transaction_a, change_blocks, &change_stats));
	assert (status4 == 0);
	MDB_stat state_v0_stats;
	auto status5 (mdb_stat (transaction_a, state_blocks_v0, &state_v0_stats));
	assert (status5 == 0);
	MDB_stat state_v1_stats;
	auto status6 (mdb_stat (transaction_a, state_blocks_v1, &state_v1_stats));
	assert (status6 == 0);
	result.send = send_stats.ms_entries;
	result.receive = receive_stats.ms_entries;
	result.open = open_stats.ms_entries;
	result.change = change_stats.ms_entries;
	result.state_v0 = state_v0_stats.ms_entries;
	result.state_v1 = state_v1_stats.ms_entries;
	return result;
}

bool rai::block_store::root_exists (MDB_txn * transaction_a, rai::uint256_union const & root_a)
{
	return block_exists (transaction_a, root_a) || account_exists (transaction_a, root_a);
}

void rai::block_store::account_del (MDB_txn * transaction_a, rai::account const & account_a)
{
	auto status1 (mdb_del (transaction_a, accounts_v1, rai::mdb_val (account_a), nullptr));
	if (status1 != 0)
	{
		assert (status1 == MDB_NOTFOUND);
		auto status2 (mdb_del (transaction_a, accounts_v0, rai::mdb_val (account_a), nullptr));
		assert (status2 == 0);
	}
}

bool rai::block_store::account_exists (MDB_txn * transaction_a, rai::account const & account_a)
{
	auto iterator (latest_begin (transaction_a, account_a));
	return iterator != latest_end () && rai::account (iterator->first) == account_a;
}

bool rai::block_store::account_get (MDB_txn * transaction_a, rai::account const & account_a, rai::account_info & info_a)
{
	rai::mdb_val value;
	auto status1 (mdb_get (transaction_a, accounts_v1, rai::mdb_val (account_a), value));
	assert (status1 == 0 || status1 == MDB_NOTFOUND);
	bool result (false);
	rai::epoch epoch;
	if (status1 == 0)
	{
		epoch = rai::epoch::epoch_1;
	}
	else
	{
		auto status2 (mdb_get (transaction_a, accounts_v0, rai::mdb_val (account_a), value));
		assert (status2 == 0 || status2 == MDB_NOTFOUND);
		if (status2 == 0)
		{
			epoch = rai::epoch::epoch_0;
		}
		else
		{
			result = true;
		}
	}
	if (!result)
	{
		rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		info_a.epoch = epoch;
		info_a.deserialize (stream);
	}
	return result;
}

void rai::block_store::frontier_put (MDB_txn * transaction_a, rai::block_hash const & block_a, rai::account const & account_a)
{
	auto status (mdb_put (transaction_a, frontiers, rai::mdb_val (block_a), rai::mdb_val (account_a), 0));
	assert (status == 0);
}

rai::account rai::block_store::frontier_get (MDB_txn * transaction_a, rai::block_hash const & block_a)
{
	rai::mdb_val value;
	auto status (mdb_get (transaction_a, frontiers, rai::mdb_val (block_a), value));
	assert (status == 0 || status == MDB_NOTFOUND);
	rai::account result (0);
	if (status == 0)
	{
		result = rai::uint256_union (value);
	}
	return result;
}

void rai::block_store::frontier_del (MDB_txn * transaction_a, rai::block_hash const & block_a)
{
	auto status (mdb_del (transaction_a, frontiers, rai::mdb_val (block_a), nullptr));
	assert (status == 0);
}

size_t rai::block_store::account_count (MDB_txn * transaction_a)
{
	MDB_stat stats1;
	auto status1 (mdb_stat (transaction_a, accounts_v0, &stats1));
	assert (status1 == 0);
	MDB_stat stats2;
	auto status2 (mdb_stat (transaction_a, accounts_v1, &stats2));
	assert (status2 == 0);
	auto result (stats1.ms_entries + stats2.ms_entries);
	return result;
}

void rai::block_store::account_put (MDB_txn * transaction_a, rai::account const & account_a, rai::account_info const & info_a)
{
	MDB_dbi db;
	switch (info_a.epoch)
	{
		case rai::epoch::invalid:
		case rai::epoch::unspecified:
			assert (false);
		case rai::epoch::epoch_0:
			db = accounts_v0;
			break;
		case rai::epoch::epoch_1:
			db = accounts_v1;
			break;
	}
	auto status (mdb_put (transaction_a, db, rai::mdb_val (account_a), rai::mdb_val (info_a), 0));
	assert (status == 0);
}

void rai::block_store::pending_put (MDB_txn * transaction_a, rai::pending_key const & key_a, rai::pending_info const & pending_a)
{
	MDB_dbi db;
	switch (pending_a.epoch)
	{
		case rai::epoch::invalid:
		case rai::epoch::unspecified:
			assert (false);
		case rai::epoch::epoch_0:
			db = pending_v0;
			break;
		case rai::epoch::epoch_1:
			db = pending_v1;
			break;
	}
	auto status (mdb_put (transaction_a, db, rai::mdb_val (key_a), rai::mdb_val (pending_a), 0));
	assert (status == 0);
}

void rai::block_store::pending_del (MDB_txn * transaction_a, rai::pending_key const & key_a)
{
	auto status1 (mdb_del (transaction_a, pending_v1, mdb_val (key_a), nullptr));
	if (status1 != 0)
	{
		assert (status1 == MDB_NOTFOUND);
		auto status2 (mdb_del (transaction_a, pending_v0, mdb_val (key_a), nullptr));
		assert (status2 == 0);
	}
}

bool rai::block_store::pending_exists (MDB_txn * transaction_a, rai::pending_key const & key_a)
{
	auto iterator (pending_begin (transaction_a, key_a));
	return iterator != pending_end () && rai::pending_key (iterator->first) == key_a;
}

bool rai::block_store::pending_get (MDB_txn * transaction_a, rai::pending_key const & key_a, rai::pending_info & pending_a)
{
	rai::mdb_val value;
	auto status1 (mdb_get (transaction_a, pending_v1, mdb_val (key_a), value));
	assert (status1 == 0 || status1 == MDB_NOTFOUND);
	bool result (false);
	rai::epoch epoch;
	if (status1 == 0)
	{
		epoch = rai::epoch::epoch_1;
	}
	else
	{
		auto status2 (mdb_get (transaction_a, pending_v0, mdb_val (key_a), value));
		assert (status2 == 0 || status2 == MDB_NOTFOUND);
		if (status2 == 0)
		{
			epoch = rai::epoch::epoch_0;
		}
		else
		{
			result = true;
		}
	}
	if (!result)
	{
		rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		pending_a.epoch = epoch;
		pending_a.deserialize (stream);
	}
	return result;
}

rai::store_iterator<rai::pending_key, rai::pending_info> rai::block_store::pending_begin (MDB_txn * transaction_a, rai::pending_key const & key_a)
{
	rai::store_iterator<rai::pending_key, rai::pending_info> result (std::make_unique<rai::mdb_merge_iterator<rai::pending_key, rai::pending_info>> (transaction_a, pending_v0, pending_v1, mdb_val (key_a)));
	return result;
}

rai::store_iterator<rai::pending_key, rai::pending_info> rai::block_store::pending_begin (MDB_txn * transaction_a)
{
	rai::store_iterator<rai::pending_key, rai::pending_info> result (std::make_unique<rai::mdb_merge_iterator<rai::pending_key, rai::pending_info>> (transaction_a, pending_v0, pending_v1));
	return result;
}

rai::store_iterator<rai::pending_key, rai::pending_info> rai::block_store::pending_end ()
{
	rai::store_iterator<rai::pending_key, rai::pending_info> result (nullptr);
	return result;
}

rai::store_iterator<rai::pending_key, rai::pending_info> rai::block_store::pending_v0_begin (MDB_txn * transaction_a, rai::pending_key const & key_a)
{
	rai::store_iterator<rai::pending_key, rai::pending_info> result (std::make_unique<rai::mdb_iterator<rai::pending_key, rai::pending_info>> (transaction_a, pending_v0, mdb_val (key_a)));
	return result;
}

rai::store_iterator<rai::pending_key, rai::pending_info> rai::block_store::pending_v0_begin (MDB_txn * transaction_a)
{
	rai::store_iterator<rai::pending_key, rai::pending_info> result (std::make_unique<rai::mdb_iterator<rai::pending_key, rai::pending_info>> (transaction_a, pending_v0));
	return result;
}

rai::store_iterator<rai::pending_key, rai::pending_info> rai::block_store::pending_v0_end ()
{
	rai::store_iterator<rai::pending_key, rai::pending_info> result (nullptr);
	return result;
}

rai::store_iterator<rai::pending_key, rai::pending_info> rai::block_store::pending_v1_begin (MDB_txn * transaction_a, rai::pending_key const & key_a)
{
	rai::store_iterator<rai::pending_key, rai::pending_info> result (std::make_unique<rai::mdb_iterator<rai::pending_key, rai::pending_info>> (transaction_a, pending_v1, mdb_val (key_a)));
	return result;
}

rai::store_iterator<rai::pending_key, rai::pending_info> rai::block_store::pending_v1_begin (MDB_txn * transaction_a)
{
	rai::store_iterator<rai::pending_key, rai::pending_info> result (std::make_unique<rai::mdb_iterator<rai::pending_key, rai::pending_info>> (transaction_a, pending_v1));
	return result;
}

rai::store_iterator<rai::pending_key, rai::pending_info> rai::block_store::pending_v1_end ()
{
	rai::store_iterator<rai::pending_key, rai::pending_info> result (nullptr);
	return result;
}

void rai::block_store::block_info_put (MDB_txn * transaction_a, rai::block_hash const & hash_a, rai::block_info const & block_info_a)
{
	auto status (mdb_put (transaction_a, blocks_info, rai::mdb_val (hash_a), rai::mdb_val (block_info_a), 0));
	assert (status == 0);
}

void rai::block_store::block_info_del (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	auto status (mdb_del (transaction_a, blocks_info, rai::mdb_val (hash_a), nullptr));
	assert (status == 0);
}

bool rai::block_store::block_info_exists (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	auto iterator (block_info_begin (transaction_a, hash_a));
	return iterator != block_info_end () && rai::block_hash (iterator->first) == hash_a;
}

bool rai::block_store::block_info_get (MDB_txn * transaction_a, rai::block_hash const & hash_a, rai::block_info & block_info_a)
{
	rai::mdb_val value;
	auto status (mdb_get (transaction_a, blocks_info, rai::mdb_val (hash_a), value));
	assert (status == 0 || status == MDB_NOTFOUND);
	bool result;
	if (status == MDB_NOTFOUND)
	{
		result = true;
	}
	else
	{
		result = false;
		assert (value.size () == sizeof (block_info_a.account.bytes) + sizeof (block_info_a.balance.bytes));
		rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error1 (rai::read (stream, block_info_a.account));
		assert (!error1);
		auto error2 (rai::read (stream, block_info_a.balance));
		assert (!error2);
	}
	return result;
}

rai::uint128_t rai::block_store::representation_get (MDB_txn * transaction_a, rai::account const & account_a)
{
	rai::mdb_val value;
	auto status (mdb_get (transaction_a, representation, rai::mdb_val (account_a), value));
	assert (status == 0 || status == MDB_NOTFOUND);
	rai::uint128_t result;
	if (status == 0)
	{
		rai::uint128_union rep;
		rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error (rai::read (stream, rep));
		assert (!error);
		result = rep.number ();
	}
	else
	{
		result = 0;
	}
	return result;
}

void rai::block_store::representation_put (MDB_txn * transaction_a, rai::account const & account_a, rai::uint128_t const & representation_a)
{
	rai::uint128_union rep (representation_a);
	auto status (mdb_put (transaction_a, representation, rai::mdb_val (account_a), rai::mdb_val (rep), 0));
	assert (status == 0);
}

void rai::block_store::unchecked_clear (MDB_txn * transaction_a)
{
	auto status (mdb_drop (transaction_a, unchecked, 0));
	assert (status == 0);
}

void rai::block_store::unchecked_put (MDB_txn * transaction_a, rai::block_hash const & hash_a, std::shared_ptr<rai::block> const & block_a)
{
	// Checking if same unchecked block is already in database
	bool exists (false);
	auto block_hash (block_a->hash ());
	auto cached (unchecked_get (transaction_a, hash_a));
	for (auto i (cached.begin ()), n (cached.end ()); i != n && !exists; ++i)
	{
		if ((*i)->hash () == block_hash)
		{
			exists = true;
		}
	}
	// Inserting block if it wasn't found in database
	if (!exists)
	{
		std::lock_guard<std::mutex> lock (cache_mutex);
		unchecked_cache.insert (std::make_pair (hash_a, block_a));
	}
}

std::shared_ptr<rai::vote> rai::block_store::vote_get (MDB_txn * transaction_a, rai::account const & account_a)
{
	std::shared_ptr<rai::vote> result;
	rai::mdb_val value;
	auto status (mdb_get (transaction_a, vote, rai::mdb_val (account_a), value));
	assert (status == 0 || status == MDB_NOTFOUND);
	if (status == 0)
	{
		std::shared_ptr<rai::vote> result (value);
		assert (result != nullptr);
		return result;
	}
	return nullptr;
}

std::vector<std::shared_ptr<rai::block>> rai::block_store::unchecked_get (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	std::vector<std::shared_ptr<rai::block>> result;
	{
		std::lock_guard<std::mutex> lock (cache_mutex);
		for (auto i (unchecked_cache.find (hash_a)), n (unchecked_cache.end ()); i != n && i->first == hash_a; ++i)
		{
			result.push_back (i->second);
		}
	}
	for (auto i (unchecked_begin (transaction_a, hash_a)), n (unchecked_end ()); i != n && rai::block_hash (i->first) == hash_a; i.impl->next_dup ())
	{
		std::shared_ptr<rai::block> block (i->second);
		result.push_back (block);
	}
	return result;
}

void rai::block_store::unchecked_del (MDB_txn * transaction_a, rai::block_hash const & hash_a, std::shared_ptr<rai::block> block_a)
{
	{
		std::lock_guard<std::mutex> lock (cache_mutex);
		for (auto i (unchecked_cache.find (hash_a)), n (unchecked_cache.end ()); i != n && i->first == hash_a;)
		{
			if (*i->second == *block_a)
			{
				i = unchecked_cache.erase (i);
			}
			else
			{
				++i;
			}
		}
	}
	rai::mdb_val block (block_a);
	auto status (mdb_del (transaction_a, unchecked, rai::mdb_val (hash_a), block));
	assert (status == 0 || status == MDB_NOTFOUND);
}

size_t rai::block_store::unchecked_count (MDB_txn * transaction_a)
{
	MDB_stat unchecked_stats;
	auto status (mdb_stat (transaction_a, unchecked, &unchecked_stats));
	assert (status == 0);
	auto result (unchecked_stats.ms_entries);
	return result;
}

void rai::block_store::checksum_put (MDB_txn * transaction_a, uint64_t prefix, uint8_t mask, rai::uint256_union const & hash_a)
{
	assert ((prefix & 0xff) == 0);
	uint64_t key (prefix | mask);
	auto status (mdb_put (transaction_a, checksum, rai::mdb_val (sizeof (key), &key), rai::mdb_val (hash_a), 0));
	assert (status == 0);
}

bool rai::block_store::checksum_get (MDB_txn * transaction_a, uint64_t prefix, uint8_t mask, rai::uint256_union & hash_a)
{
	assert ((prefix & 0xff) == 0);
	uint64_t key (prefix | mask);
	rai::mdb_val value;
	auto status (mdb_get (transaction_a, checksum, rai::mdb_val (sizeof (key), &key), value));
	assert (status == 0 || status == MDB_NOTFOUND);
	bool result;
	if (status == 0)
	{
		result = false;
		rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error (rai::read (stream, hash_a));
		assert (!error);
	}
	else
	{
		result = true;
	}
	return result;
}

void rai::block_store::checksum_del (MDB_txn * transaction_a, uint64_t prefix, uint8_t mask)
{
	assert ((prefix & 0xff) == 0);
	uint64_t key (prefix | mask);
	auto status (mdb_del (transaction_a, checksum, rai::mdb_val (sizeof (key), &key), nullptr));
	assert (status == 0);
}

void rai::block_store::flush (MDB_txn * transaction_a)
{
	std::unordered_map<rai::account, std::shared_ptr<rai::vote>> sequence_cache_l;
	std::unordered_multimap<rai::block_hash, std::shared_ptr<rai::block>> unchecked_cache_l;
	{
		std::lock_guard<std::mutex> lock (cache_mutex);
		sequence_cache_l.swap (vote_cache);
		unchecked_cache_l.swap (unchecked_cache);
	}
	for (auto & i : unchecked_cache_l)
	{
		mdb_val block (i.second);
		auto status (mdb_put (transaction_a, unchecked, rai::mdb_val (i.first), block, 0));
		assert (status == 0);
	}
	for (auto i (sequence_cache_l.begin ()), n (sequence_cache_l.end ()); i != n; ++i)
	{
		std::vector<uint8_t> vector;
		{
			rai::vectorstream stream (vector);
			i->second->serialize (stream);
		}
		auto status1 (mdb_put (transaction_a, vote, rai::mdb_val (i->first), rai::mdb_val (vector.size (), vector.data ()), 0));
		assert (status1 == 0);
	}
}
std::shared_ptr<rai::vote> rai::block_store::vote_current (MDB_txn * transaction_a, rai::account const & account_a)
{
	assert (!cache_mutex.try_lock ());
	std::shared_ptr<rai::vote> result;
	auto existing (vote_cache.find (account_a));
	if (existing != vote_cache.end ())
	{
		result = existing->second;
	}
	else
	{
		result = vote_get (transaction_a, account_a);
	}
	return result;
}

std::shared_ptr<rai::vote> rai::block_store::vote_generate (MDB_txn * transaction_a, rai::account const & account_a, rai::raw_key const & key_a, std::shared_ptr<rai::block> block_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto result (vote_current (transaction_a, account_a));
	uint64_t sequence ((result ? result->sequence : 0) + 1);
	result = std::make_shared<rai::vote> (account_a, key_a, sequence, block_a);
	vote_cache[account_a] = result;
	return result;
}

std::shared_ptr<rai::vote> rai::block_store::vote_generate (MDB_txn * transaction_a, rai::account const & account_a, rai::raw_key const & key_a, std::vector<rai::block_hash> blocks_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto result (vote_current (transaction_a, account_a));
	uint64_t sequence ((result ? result->sequence : 0) + 1);
	result = std::make_shared<rai::vote> (account_a, key_a, sequence, blocks_a);
	vote_cache[account_a] = result;
	return result;
}

std::shared_ptr<rai::vote> rai::block_store::vote_max (MDB_txn * transaction_a, std::shared_ptr<rai::vote> vote_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto current (vote_current (transaction_a, vote_a->account));
	auto result (vote_a);
	if (current != nullptr)
	{
		if (current->sequence > result->sequence)
		{
			result = current;
		}
	}
	vote_cache[vote_a->account] = result;
	return result;
}

rai::store_iterator<rai::account, rai::account_info> rai::block_store::latest_begin (MDB_txn * transaction_a, rai::account const & account_a)
{
	rai::store_iterator<rai::account, rai::account_info> result (std::make_unique<rai::mdb_merge_iterator<rai::account, rai::account_info>> (transaction_a, accounts_v0, accounts_v1, rai::mdb_val (account_a)));
	return result;
}

rai::store_iterator<rai::account, rai::account_info> rai::block_store::latest_begin (MDB_txn * transaction_a)
{
	rai::store_iterator<rai::account, rai::account_info> result (std::make_unique<rai::mdb_merge_iterator<rai::account, rai::account_info>> (transaction_a, accounts_v0, accounts_v1));
	return result;
}

rai::store_iterator<rai::account, rai::account_info> rai::block_store::latest_end ()
{
	rai::store_iterator<rai::account, rai::account_info> result (nullptr);
	return result;
}

rai::store_iterator<rai::account, rai::account_info> rai::block_store::latest_v0_begin (MDB_txn * transaction_a, rai::account const & account_a)
{
	rai::store_iterator<rai::account, rai::account_info> result (std::make_unique<rai::mdb_iterator<rai::account, rai::account_info>> (transaction_a, accounts_v0, rai::mdb_val (account_a)));
	return result;
}

rai::store_iterator<rai::account, rai::account_info> rai::block_store::latest_v0_begin (MDB_txn * transaction_a)
{
	rai::store_iterator<rai::account, rai::account_info> result (std::make_unique<rai::mdb_iterator<rai::account, rai::account_info>> (transaction_a, accounts_v0));
	return result;
}

rai::store_iterator<rai::account, rai::account_info> rai::block_store::latest_v0_end ()
{
	rai::store_iterator<rai::account, rai::account_info> result (nullptr);
	return result;
}

rai::store_iterator<rai::account, rai::account_info> rai::block_store::latest_v1_begin (MDB_txn * transaction_a, rai::account const & account_a)
{
	rai::store_iterator<rai::account, rai::account_info> result (std::make_unique<rai::mdb_iterator<rai::account, rai::account_info>> (transaction_a, accounts_v1, rai::mdb_val (account_a)));
	return result;
}

rai::store_iterator<rai::account, rai::account_info> rai::block_store::latest_v1_begin (MDB_txn * transaction_a)
{
	rai::store_iterator<rai::account, rai::account_info> result (std::make_unique<rai::mdb_iterator<rai::account, rai::account_info>> (transaction_a, accounts_v1));
	return result;
}

rai::store_iterator<rai::account, rai::account_info> rai::block_store::latest_v1_end ()
{
	rai::store_iterator<rai::account, rai::account_info> result (nullptr);
	return result;
}
