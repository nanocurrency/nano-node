#include <rai/secure.hpp>

#include <rai/lib/interface.h>
#include <rai/node/common.hpp>
#include <rai/node/working.hpp>
#include <rai/versioning.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <queue>

#include <ed25519-donna/ed25519.h>

// Genesis keys for network variants
namespace
{
char const * test_private_key_data = "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4";
char const * test_public_key_data = "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0"; // xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo
char const * beta_public_key_data = "9D3A5B66B478670455B241D6BAC3D3FE1CBB7E7B7EAA429FA036C2704C3DC0A4"; // xrb_39btdfmday591jcu6igpqd3x9ziwqfz9pzocacht1fp4g385ui76a87x6phk
char const * live_public_key_data = "E89208DD038FBB269987689621D52292AE9C35941A7484756ECCED92A65093BA"; // xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3
char const * test_genesis_data = R"%%%({
	"type": "open",
	"source": "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0",
	"representative": "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"account": "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"work": "9680625b39d3363d",
	"signature": "ECDA914373A2F0CA1296475BAEE40500A7F0A7AD72A5A80C81D7FAB7F6C802B2CC7DB50F5DD0FB25B2EF11761FA7344A158DD5A700B21BD47DE5BD0F63153A02"
})%%%";

char const * beta_genesis_data = R"%%%({
	"type": "open",
	"source": "9D3A5B66B478670455B241D6BAC3D3FE1CBB7E7B7EAA429FA036C2704C3DC0A4",
	"representative": "xrb_39btdfmday591jcu6igpqd3x9ziwqfz9pzocacht1fp4g385ui76a87x6phk",
	"account": "xrb_39btdfmday591jcu6igpqd3x9ziwqfz9pzocacht1fp4g385ui76a87x6phk",
	"work": "6eb12d4c42dba31e",
	"signature": "BD0D374FCEB33EAABDF728E9B4DCDBF3B226DA97EEAB8EA5B7EDE286B1282C24D6EB544644FE871235E4F58CD94DF66D9C555309895F67A7D1F922AAC12CE907"
})%%%";

char const * live_genesis_data = R"%%%({
	"type": "open",
	"source": "E89208DD038FBB269987689621D52292AE9C35941A7484756ECCED92A65093BA",
	"representative": "xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3",
	"account": "xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3",
	"work": "62f05417dd3fb691",
	"signature": "9F0C933C8ADE004D808EA1985FA746A7E95BA2A38F867640F53EC8F180BDFE9E2C1268DEAD7C2664F356E37ABA362BC58E46DBA03E523A7B5A19E4B6EB12BB02"
})%%%";

class ledger_constants
{
public:
	ledger_constants () :
	zero_key ("0"),
	test_genesis_key (test_private_key_data),
	rai_test_account (test_public_key_data),
	rai_beta_account (beta_public_key_data),
	rai_live_account (live_public_key_data),
	rai_test_genesis (test_genesis_data),
	rai_beta_genesis (beta_genesis_data),
	rai_live_genesis (live_genesis_data),
	genesis_account (rai::rai_network == rai::rai_networks::rai_test_network ? rai_test_account : rai::rai_network == rai::rai_networks::rai_beta_network ? rai_beta_account : rai_live_account),
	genesis_block (rai::rai_network == rai::rai_networks::rai_test_network ? rai_test_genesis : rai::rai_network == rai::rai_networks::rai_beta_network ? rai_beta_genesis : rai_live_genesis),
	genesis_amount (std::numeric_limits<rai::uint128_t>::max ()),
	burn_account (0)
	{
		CryptoPP::AutoSeededRandomPool random_pool;
		// Randomly generating these mean no two nodes will ever have the same sentinal values which protects against some insecure algorithms
		random_pool.GenerateBlock (not_a_block.bytes.data (), not_a_block.bytes.size ());
		random_pool.GenerateBlock (not_an_account.bytes.data (), not_an_account.bytes.size ());
	}
	rai::keypair zero_key;
	rai::keypair test_genesis_key;
	rai::account rai_test_account;
	rai::account rai_beta_account;
	rai::account rai_live_account;
	std::string rai_test_genesis;
	std::string rai_beta_genesis;
	std::string rai_live_genesis;
	rai::account genesis_account;
	std::string genesis_block;
	rai::uint128_t genesis_amount;
	rai::block_hash not_a_block;
	rai::account not_an_account;
	rai::account burn_account;
};
ledger_constants globals;
}

size_t constexpr rai::send_block::size;
size_t constexpr rai::receive_block::size;
size_t constexpr rai::open_block::size;
size_t constexpr rai::change_block::size;

rai::keypair const & rai::zero_key (globals.zero_key);
rai::keypair const & rai::test_genesis_key (globals.test_genesis_key);
rai::account const & rai::rai_test_account (globals.rai_test_account);
rai::account const & rai::rai_beta_account (globals.rai_beta_account);
rai::account const & rai::rai_live_account (globals.rai_live_account);
std::string const & rai::rai_test_genesis (globals.rai_test_genesis);
std::string const & rai::rai_beta_genesis (globals.rai_beta_genesis);
std::string const & rai::rai_live_genesis (globals.rai_live_genesis);

rai::account const & rai::genesis_account (globals.genesis_account);
std::string const & rai::genesis_block (globals.genesis_block);
rai::uint128_t const & rai::genesis_amount (globals.genesis_amount);
rai::block_hash const & rai::not_a_block (globals.not_a_block);
rai::block_hash const & rai::not_an_account (globals.not_an_account);
rai::account const & rai::burn_account (globals.burn_account);

boost::filesystem::path rai::working_path ()
{
	auto result (rai::app_path ());
	switch (rai::rai_network)
	{
		case rai::rai_networks::rai_test_network:
			result /= "RaiBlocksTest";
			break;
		case rai::rai_networks::rai_beta_network:
			result /= "RaiBlocksBeta";
			break;
		case rai::rai_networks::rai_live_network:
			result /= "RaiBlocks";
			break;
	}
	return result;
}

size_t rai::shared_ptr_block_hash::operator() (std::shared_ptr<rai::block> const & block_a) const
{
	auto hash (block_a->hash ());
	auto result (static_cast<size_t> (hash.qwords[0]));
	return result;
}

bool rai::shared_ptr_block_hash::operator() (std::shared_ptr<rai::block> const & lhs, std::shared_ptr<rai::block> const & rhs) const
{
	return *lhs == *rhs;
}

// Sum the weights for each vote and return the winning block with its vote tally
std::pair<rai::uint128_t, std::shared_ptr<rai::block>> rai::ledger::winner (MDB_txn * transaction_a, rai::votes const & votes_a)
{
	auto tally_l (tally (transaction_a, votes_a));
	auto existing (tally_l.begin ());
	return std::make_pair (existing->first, existing->second);
}

std::map<rai::uint128_t, std::shared_ptr<rai::block>, std::greater<rai::uint128_t>> rai::ledger::tally (MDB_txn * transaction_a, rai::votes const & votes_a)
{
	std::unordered_map<std::shared_ptr<block>, rai::uint128_t, rai::shared_ptr_block_hash, rai::shared_ptr_block_hash> totals;
	// Construct a map of blocks -> vote total.
	for (auto & i : votes_a.rep_votes)
	{
		auto existing (totals.find (i.second));
		if (existing == totals.end ())
		{
			totals.insert (std::make_pair (i.second, 0));
			existing = totals.find (i.second);
			assert (existing != totals.end ());
		}
		auto weight_l (weight (transaction_a, i.first));
		existing->second += weight_l;
	}
	// Construction a map of vote total -> block in decreasing order.
	std::map<rai::uint128_t, std::shared_ptr<rai::block>, std::greater<rai::uint128_t>> result;
	for (auto & i : totals)
	{
		result[i.second] = i.first;
	}
	return result;
}

rai::votes::votes (std::shared_ptr<rai::block> block_a) :
id (block_a->root ())
{
	rep_votes.insert (std::make_pair (rai::not_an_account, block_a));
}

rai::tally_result rai::votes::vote (std::shared_ptr<rai::vote> vote_a)
{
	rai::tally_result result;
	auto existing (rep_votes.find (vote_a->account));
	if (existing == rep_votes.end ())
	{
		// Vote on this block hasn't been seen from rep before
		result = rai::tally_result::vote;
		rep_votes.insert (std::make_pair (vote_a->account, vote_a->block));
	}
	else
	{
		if (!(*existing->second == *vote_a->block))
		{
			// Rep changed their vote
			result = rai::tally_result::changed;
			existing->second = vote_a->block;
		}
		else
		{
			// Rep vote remained the same
			result = rai::tally_result::confirm;
		}
	}
	return result;
}

// Create a new random keypair
rai::keypair::keypair ()
{
	random_pool.GenerateBlock (prv.data.bytes.data (), prv.data.bytes.size ());
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a hex string of the private key
rai::keypair::keypair (std::string const & prv_a)
{
	auto error (prv.data.decode_hex (prv_a));
	assert (!error);
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

rai::ledger::ledger (rai::block_store & store_a, rai::uint128_t const & inactive_supply_a) :
store (store_a),
inactive_supply (inactive_supply_a),
check_bootstrap_weights (true)
{
}

// Serialize a block prefixed with an 8-bit typecode
void rai::serialize_block (rai::stream & stream_a, rai::block const & block_a)
{
	write (stream_a, block_a.type ());
	block_a.serialize (stream_a);
}

std::unique_ptr<rai::block> rai::deserialize_block (MDB_val const & val_a)
{
	rai::bufferstream stream (reinterpret_cast<uint8_t const *> (val_a.mv_data), val_a.mv_size);
	return deserialize_block (stream);
}

rai::account_info::account_info () :
head (0),
rep_block (0),
open_block (0),
balance (0),
modified (0),
block_count (0)
{
}

rai::account_info::account_info (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (head) + sizeof (rep_block) + sizeof (open_block) + sizeof (balance) + sizeof (modified) + sizeof (block_count) == sizeof (*this), "Class not packed");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

rai::account_info::account_info (rai::block_hash const & head_a, rai::block_hash const & rep_block_a, rai::block_hash const & open_block_a, rai::amount const & balance_a, uint64_t modified_a, uint64_t block_count_a) :
head (head_a),
rep_block (rep_block_a),
open_block (open_block_a),
balance (balance_a),
modified (modified_a),
block_count (block_count_a)
{
}

void rai::account_info::serialize (rai::stream & stream_a) const
{
	write (stream_a, head.bytes);
	write (stream_a, rep_block.bytes);
	write (stream_a, open_block.bytes);
	write (stream_a, balance.bytes);
	write (stream_a, modified);
	write (stream_a, block_count);
}

bool rai::account_info::deserialize (rai::stream & stream_a)
{
	auto error (read (stream_a, head.bytes));
	if (!error)
	{
		error = read (stream_a, rep_block.bytes);
		if (!error)
		{
			error = read (stream_a, open_block.bytes);
			if (!error)
			{
				error = read (stream_a, balance.bytes);
				if (!error)
				{
					error = read (stream_a, modified);
					if (!error)
					{
						error = read (stream_a, block_count);
					}
				}
			}
		}
	}
	return error;
}

bool rai::account_info::operator== (rai::account_info const & other_a) const
{
	return head == other_a.head && rep_block == other_a.rep_block && open_block == other_a.open_block && balance == other_a.balance && modified == other_a.modified && block_count == other_a.block_count;
}

bool rai::account_info::operator!= (rai::account_info const & other_a) const
{
	return !(*this == other_a);
}

rai::mdb_val rai::account_info::val () const
{
	return rai::mdb_val (sizeof (*this), const_cast<rai::account_info *> (this));
}

rai::store_entry::store_entry () :
first (0, nullptr),
second (0, nullptr)
{
}

void rai::store_entry::clear ()
{
	first = { 0, nullptr };
	second = { 0, nullptr };
}

rai::store_entry * rai::store_entry::operator-> ()
{
	return this;
}

rai::store_entry & rai::store_iterator::operator-> ()
{
	return current;
}

rai::store_iterator::store_iterator (MDB_txn * transaction_a, MDB_dbi db_a) :
cursor (nullptr)
{
	auto status (mdb_cursor_open (transaction_a, db_a, &cursor));
	assert (status == 0);
	auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_FIRST));
	assert (status2 == 0 || status2 == MDB_NOTFOUND);
	if (status2 != MDB_NOTFOUND)
	{
		auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
		assert (status3 == 0 || status3 == MDB_NOTFOUND);
	}
	else
	{
		current.clear ();
	}
}

rai::store_iterator::store_iterator (std::nullptr_t) :
cursor (nullptr)
{
}

rai::store_iterator::store_iterator (MDB_txn * transaction_a, MDB_dbi db_a, MDB_val const & val_a) :
cursor (nullptr)
{
	auto status (mdb_cursor_open (transaction_a, db_a, &cursor));
	assert (status == 0);
	current.first.value = val_a;
	auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_SET_RANGE));
	assert (status2 == 0 || status2 == MDB_NOTFOUND);
	if (status2 != MDB_NOTFOUND)
	{
		auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
		assert (status3 == 0 || status3 == MDB_NOTFOUND);
	}
	else
	{
		current.clear ();
	}
}

rai::store_iterator::store_iterator (rai::store_iterator && other_a)
{
	cursor = other_a.cursor;
	other_a.cursor = nullptr;
	current = other_a.current;
}

rai::store_iterator::~store_iterator ()
{
	if (cursor != nullptr)
	{
		mdb_cursor_close (cursor);
	}
}

rai::store_iterator & rai::store_iterator::operator++ ()
{
	assert (cursor != nullptr);
	auto status (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_NEXT));
	if (status == MDB_NOTFOUND)
	{
		current.clear ();
	}
	return *this;
}

void rai::store_iterator::next_dup ()
{
	assert (cursor != nullptr);
	auto status (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_NEXT_DUP));
	if (status == MDB_NOTFOUND)
	{
		current.clear ();
	}
}

rai::store_iterator & rai::store_iterator::operator= (rai::store_iterator && other_a)
{
	if (cursor != nullptr)
	{
		mdb_cursor_close (cursor);
	}
	cursor = other_a.cursor;
	other_a.cursor = nullptr;
	current = other_a.current;
	other_a.current.clear ();
	return *this;
}

bool rai::store_iterator::operator== (rai::store_iterator const & other_a) const
{
	auto result (current.first.data () == other_a.current.first.data ());
	assert (!result || (current.first.size () == other_a.current.first.size ()));
	assert (!result || (current.second.data () == other_a.current.second.data ()));
	assert (!result || (current.second.size () == other_a.current.second.size ()));
	return result;
}

bool rai::store_iterator::operator!= (rai::store_iterator const & other_a) const
{
	return !(*this == other_a);
}

rai::block_counts::block_counts () :
send (0),
receive (0),
open (0),
change (0)
{
}

size_t rai::block_counts::sum ()
{
	return send + receive + open + change;
}

rai::block_store::block_store (bool & error_a, boost::filesystem::path const & path_a, int lmdb_max_dbs) :
environment (error_a, path_a, lmdb_max_dbs),
frontiers (0),
accounts (0),
send_blocks (0),
receive_blocks (0),
open_blocks (0),
change_blocks (0),
pending (0),
blocks_info (0),
representation (0),
unchecked (0),
unsynced (0),
checksum (0)
{
	if (!error_a)
	{
		rai::transaction transaction (environment, nullptr, true);
		error_a |= mdb_dbi_open (transaction, "frontiers", MDB_CREATE, &frontiers) != 0;
		error_a |= mdb_dbi_open (transaction, "accounts", MDB_CREATE, &accounts) != 0;
		error_a |= mdb_dbi_open (transaction, "send", MDB_CREATE, &send_blocks) != 0;
		error_a |= mdb_dbi_open (transaction, "receive", MDB_CREATE, &receive_blocks) != 0;
		error_a |= mdb_dbi_open (transaction, "open", MDB_CREATE, &open_blocks) != 0;
		error_a |= mdb_dbi_open (transaction, "change", MDB_CREATE, &change_blocks) != 0;
		error_a |= mdb_dbi_open (transaction, "pending", MDB_CREATE, &pending) != 0;
		error_a |= mdb_dbi_open (transaction, "blocks_info", MDB_CREATE, &blocks_info) != 0;
		error_a |= mdb_dbi_open (transaction, "representation", MDB_CREATE, &representation) != 0;
		error_a |= mdb_dbi_open (transaction, "unchecked", MDB_CREATE | MDB_DUPSORT, &unchecked) != 0;
		error_a |= mdb_dbi_open (transaction, "unsynced", MDB_CREATE, &unsynced) != 0;
		error_a |= mdb_dbi_open (transaction, "checksum", MDB_CREATE, &checksum) != 0;
		error_a |= mdb_dbi_open (transaction, "vote", MDB_CREATE, &vote) != 0;
		error_a |= mdb_dbi_open (transaction, "meta", MDB_CREATE, &meta) != 0;
		error_a |= mdb_dbi_open (transaction, "hash2", MDB_CREATE, &hash2) != 0;
		if (!error_a)
		{
			do_upgrades (transaction);
			checksum_put (transaction, 0, 0, 0);
		}
	}
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
		rai::uint256_union version_value (data.uint256 ());
		assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
		result = version_value.number ().convert_to<int> ();
	}
	return result;
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
			hash2_put (transaction_a, genesis_account, genesis_account);
			// Version number is changed in background update
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
		rai::store_iterator i (transaction_a, accounts, rai::mdb_val (account));
		std::cerr << std::hex;
		if (i != rai::store_iterator (nullptr))
		{
			account = i->first.uint256 ();
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
			auto status (mdb_put (transaction_a, accounts, rai::mdb_val (account), v2.val (), 0));
			assert (status == 0);
			account = account.number () + 1;
		}
		else
		{
			account.clear ();
		}
	}
}

// Determine the representative for this block
class representative_visitor : public rai::block_visitor
{
public:
	representative_visitor (MDB_txn * transaction_a, rai::block_store & store_a) :
	transaction (transaction_a),
	store (store_a),
	result (0)
	{
	}
	virtual ~representative_visitor () = default;
	void compute (rai::block_hash const & hash_a)
	{
		current = hash_a;
		while (result.is_zero ())
		{
			auto block (store.block_get (transaction, current));
			assert (block != nullptr);
			block->visit (*this);
		}
	}
	void send_block (rai::send_block const & block_a) override
	{
		current = block_a.previous ();
	}
	void receive_block (rai::receive_block const & block_a) override
	{
		current = block_a.previous ();
	}
	void open_block (rai::open_block const & block_a) override
	{
		result = block_a.hash ();
	}
	void change_block (rai::change_block const & block_a) override
	{
		result = block_a.hash ();
	}
	MDB_txn * transaction;
	rai::block_store & store;
	rai::block_hash current;
	rai::block_hash result;
};

void rai::block_store::upgrade_v2_to_v3 (MDB_txn * transaction_a)
{
	version_put (transaction_a, 3);
	mdb_drop (transaction_a, representation, 0);
	for (auto i (latest_begin (transaction_a)), n (latest_end ()); i != n; ++i)
	{
		rai::account account_l (i->first.uint256 ());
		rai::account_info_v5 info (i->second);
		representative_visitor visitor (transaction_a, *this);
		visitor.compute (info.head);
		assert (!visitor.result.is_zero ());
		info.rep_block = visitor.result;
		mdb_cursor_put (i.cursor, rai::mdb_val (account_l), info.val (), MDB_CURRENT);
		representation_add (transaction_a, visitor.result, info.balance.number ());
	}
}

void rai::block_store::upgrade_v3_to_v4 (MDB_txn * transaction_a)
{
	version_put (transaction_a, 4);
	std::queue<std::pair<rai::pending_key, rai::pending_info>> items;
	for (auto i (pending_begin (transaction_a)), n (pending_end ()); i != n; ++i)
	{
		rai::block_hash hash (i->first.uint256 ());
		rai::pending_info_v3 info (i->second);
		items.push (std::make_pair (rai::pending_key (info.destination, hash), rai::pending_info (info.source, info.amount)));
	}
	mdb_drop (transaction_a, pending, 0);
	while (!items.empty ())
	{
		pending_put (transaction_a, items.front ().first, items.front ().second);
		items.pop ();
	}
}

void rai::block_store::upgrade_v4_to_v5 (MDB_txn * transaction_a)
{
	version_put (transaction_a, 5);
	for (auto i (latest_begin (transaction_a)), n (latest_end ()); i != n; ++i)
	{
		rai::account account (i->first.uint256 ());
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
	for (auto i (latest_begin (transaction_a)), n (latest_end ()); i != n; ++i)
	{
		rai::account account (i->first.uint256 ());
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
		rai::account_info info (info_old.head, info_old.rep_block, info_old.open_block, info_old.balance, info_old.modified, block_count);
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
	for (rai::store_iterator i (transaction_a, sequence), n (nullptr); i != n; ++i)
	{
		rai::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
		uint64_t sequence;
		auto error (rai::read (stream, sequence));
		// Create a dummy vote with the same sequence number for easy upgrading.  This won't have a valid signature.
		auto dummy (std::make_shared<rai::vote> (rai::account (i->first.uint256 ()), junk.prv, sequence, block));
		std::vector<uint8_t> vector;
		{
			rai::vectorstream stream (vector);
			dummy->serialize (stream);
		}
		auto status1 (mdb_put (transaction_a, vote, i->first, rai::mdb_val (vector.size (), vector.data ()), 0));
		assert (status1 == 0);
		assert (!error);
	}
	mdb_drop (transaction_a, sequence, 1);
}

void rai::block_store::upgrade_v9_to_v10 (MDB_txn * transaction_a)
{
	//std::cerr << boost::str (boost::format ("Performing database upgrade to version 10...\n"));
	version_put (transaction_a, 10);
	for (auto i (latest_begin (transaction_a)), n (latest_end ()); i != n; ++i)
	{
		rai::account_info info (i->second);
		if (info.block_count >= block_info_max)
		{
			rai::account account (i->first.uint256 ());
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

void rai::block_store::upgrade_v10_to_v11 ()
{
	std::unordered_multimap<rai::block_hash, rai::block_hash> dependencies;
	{
		rai::transaction transaction (environment, nullptr, false);
		dependencies = (block_dependencies (transaction));
	}
	std::deque<rai::block_hash> remaining;
	rai::genesis genesis;
	remaining.push_back (genesis.hash ());
	auto total (0);
	while (!remaining.empty ())
	{
		auto count (0);
		//std::cerr << boost::str (boost::format ("Upgraded %1%\n") % total);
		rai::transaction transaction (environment, nullptr, true);
		while (!remaining.empty () && count < 16 * 1024)
		{
			auto hash1 (remaining.front ());
			remaining.pop_front ();
			if (hash2_get (transaction, hash1).is_zero ())
			{
				auto range (dependencies.equal_range (hash1));
				auto block (block_get (transaction, hash1));
				assert (block != nullptr);
				auto hash2_new (hash2_calc (transaction, *block));
				if (!hash2_new.is_zero ())
				{
					hash2_put (transaction, hash1, hash2_new);
					// Save the hash1 in the mapping table and remap block to hash2
					auto successor (block_successor (transaction, hash1));
					block_del (transaction, hash1);
					block_put (transaction, hash2_new, *block, successor);
					std::for_each (range.first, range.second, [&](std::pair<rai::block_hash, rai::block_hash> item_a) {
						remaining.push_front (item_a.second);
					});
					++count;
				}
				else
				{
					remaining.push_back (hash1);
				}
			}
		}
		total += count;
	}
	rai::transaction transaction (environment, nullptr, true);
	auto count (block_count (transaction));
	assert (count.hash2 == count.sum () + 1);
	version_put (transaction, 11);
}

void rai::block_store::clear (MDB_dbi db_a)
{
	rai::transaction transaction (environment, nullptr, true);
	auto status (mdb_drop (transaction, db_a, 0));
	assert (status == 0);
}

namespace
{
// Fill in our predecessors
class set_predecessor : public rai::block_visitor
{
public:
	set_predecessor (MDB_txn * transaction_a, rai::block_store & store_a) :
	transaction (transaction_a),
	store (store_a)
	{
	}
	virtual ~set_predecessor () = default;
	void fill_value (rai::block const & block_a)
	{
		auto hash (block_a.hash ());
		rai::block_type type;
		auto value (store.block_get_raw (transaction, block_a.previous (), type));
		assert (value.mv_size != 0);
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.mv_data), static_cast<uint8_t *> (value.mv_data) + value.mv_size);
		std::copy (hash.bytes.begin (), hash.bytes.end (), data.end () - hash.bytes.size ());
		store.block_put_raw (transaction, store.block_database (type), block_a.previous (), rai::mdb_val (data.size (), data.data ()));
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
	MDB_txn * transaction;
	rai::block_store & store;
};
}

MDB_dbi rai::block_store::block_database (rai::block_type type_a)
{
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
		default:
			assert (false);
			break;
	}
	return result;
}

void rai::block_store::block_put_raw (MDB_txn * transaction_a, MDB_dbi database_a, rai::block_hash const & hash_a, MDB_val value_a)
{
	auto hash (hash2_get (transaction_a, hash_a));
	if (hash.is_zero ())
	{
		hash = hash_a;
	}
	auto status2 (mdb_put (transaction_a, database_a, rai::mdb_val (hash), &value_a, 0));
	assert (status2 == 0);
}

void rai::block_store::block_put (MDB_txn * transaction_a, rai::block_hash const & hash_a, rai::block const & block_a, rai::block_hash const & successor_a)
{
	assert (successor_a.is_zero () || block_exists (transaction_a, successor_a));
	std::vector<uint8_t> vector;
	{
		rai::vectorstream stream (vector);
		block_a.serialize (stream);
		rai::write (stream, successor_a.bytes);
	}
	block_put_raw (transaction_a, block_database (block_a.type ()), hash_a, { vector.size (), vector.data () });
	set_predecessor predecessor (transaction_a, *this);
	block_a.visit (predecessor);
}

MDB_val rai::block_store::block_get_raw (MDB_txn * transaction_a, rai::block_hash const & hash_a, rai::block_type & type_a)
{
	auto hash (hash2_get (transaction_a, hash_a));
	if (hash.is_zero ())
	{
		hash = hash_a;
	}
	rai::mdb_val result;
	auto status (mdb_get (transaction_a, send_blocks, rai::mdb_val (hash), result));
	assert (status == 0 || status == MDB_NOTFOUND);
	if (status != 0)
	{
		auto status (mdb_get (transaction_a, receive_blocks, rai::mdb_val (hash), result));
		assert (status == 0 || status == MDB_NOTFOUND);
		if (status != 0)
		{
			auto status (mdb_get (transaction_a, open_blocks, rai::mdb_val (hash), result));
			assert (status == 0 || status == MDB_NOTFOUND);
			if (status != 0)
			{
				auto status (mdb_get (transaction_a, change_blocks, rai::mdb_val (hash), result));
				assert (status == 0 || status == MDB_NOTFOUND);
				if (status == 0)
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
	assert ((hash_a == hash || result != nullptr) && "Missing hash2 mapping");
	return result;
}

std::unique_ptr<rai::block> rai::block_store::block_random (MDB_txn * transaction_a, MDB_dbi database)
{
	rai::block_hash hash;
	rai::random_pool.GenerateBlock (hash.bytes.data (), hash.bytes.size ());
	rai::store_iterator existing (transaction_a, database, rai::mdb_val (hash));
	if (existing == rai::store_iterator (nullptr))
	{
		existing = rai::store_iterator (transaction_a, database);
	}
	assert (existing != rai::store_iterator (nullptr));
	return block_get (transaction_a, rai::block_hash (existing->first.uint256 ()));
}

std::unique_ptr<rai::block> rai::block_store::block_random (MDB_txn * transaction_a)
{
	auto count (block_count (transaction_a));
	auto region (rai::random_pool.GenerateWord32 (0, count.sum () - 1));
	std::unique_ptr<rai::block> result;
	if (region < count.send)
	{
		result = block_random (transaction_a, send_blocks);
	}
	else
	{
		region -= count.send;
		if (region < count.receive)
		{
			result = block_random (transaction_a, receive_blocks);
		}
		else
		{
			region -= count.receive;
			if (region < count.open)
			{
				result = block_random (transaction_a, open_blocks);
			}
			else
			{
				// change
				result = block_random (transaction_a, change_blocks);
			}
		}
	}
	return result;
}

rai::block_hash rai::block_store::block_successor (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	rai::block_type type;
	auto value (block_get_raw (transaction_a, hash_a, type));
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
	block_put (transaction_a, hash_a, *block);
}

std::unique_ptr<rai::block> rai::block_store::block_get (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	rai::block_type type;
	auto value (block_get_raw (transaction_a, hash_a, type));
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

bool rai::block_store::block_exists (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	auto exists (true);
	auto hash (hash2_get (transaction_a, hash_a));
	if (hash.is_zero ())
	{
		hash = hash_a;
	}
	rai::mdb_val junk;
	auto status (mdb_get (transaction_a, send_blocks, rai::mdb_val (hash), junk));
	assert (status == 0 || status == MDB_NOTFOUND);
	exists = status == 0;
	if (!exists)
	{
		auto status (mdb_get (transaction_a, receive_blocks, rai::mdb_val (hash), junk));
		assert (status == 0 || status == MDB_NOTFOUND);
		exists = status == 0;
		if (!exists)
		{
			auto status (mdb_get (transaction_a, open_blocks, rai::mdb_val (hash), junk));
			assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			if (!exists)
			{
				auto status (mdb_get (transaction_a, change_blocks, rai::mdb_val (hash), junk));
				assert (status == 0 || status == MDB_NOTFOUND);
				exists = status == 0;
			}
		}
	}
	// Mapping in hash2 table implies block exists
	assert ((hash_a == hash || exists) && "Missing hash2 mapping");
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
	MDB_stat hash2_stats;
	auto status5 (mdb_stat (transaction_a, hash2, &hash2_stats));
	assert (status5 == 0);
	result.send = send_stats.ms_entries;
	result.receive = receive_stats.ms_entries;
	result.open = open_stats.ms_entries;
	result.change = change_stats.ms_entries;
	result.hash2 = hash2_stats.ms_entries;
	return result;
}

std::unordered_multimap<rai::block_hash, rai::block_hash> rai::block_store::block_dependencies (MDB_txn * transaction_a)
{
	std::unordered_multimap<rai::block_hash, rai::block_hash> result;
	// For every block type
	for (auto type : { rai::block_type::send, rai::block_type::receive, rai::block_type::open, rai::block_type::change })
	{
		auto db (block_database (type));
		// For every block in that type's table
		for (auto i (rai::store_iterator (transaction_a, db)), n (rai::store_iterator (nullptr)); i != n; ++i)
		{
			rai::block_hash hash (i->first.uint256 ());
			auto block (block_get (transaction_a, hash));
			if (type != rai::block_type::open)
			{
				auto previous (block->previous ());
				assert (!previous.is_zero ());
				result.insert (std::make_pair (previous, hash));
			}
			if (type == rai::block_type::open || type == rai::block_type::receive)
			{
				auto source (block->source ());
				assert (!source.is_zero ());
				result.insert (std::make_pair (source, hash));
			}
		}
	}
	return result;
}

void rai::block_store::account_del (MDB_txn * transaction_a, rai::account const & account_a)
{
	auto status (mdb_del (transaction_a, accounts, rai::mdb_val (account_a), nullptr));
	assert (status == 0);
}

bool rai::block_store::account_exists (MDB_txn * transaction_a, rai::account const & account_a)
{
	auto iterator (latest_begin (transaction_a, account_a));
	return iterator != rai::store_iterator (nullptr) && rai::account (iterator->first.uint256 ()) == account_a;
}

bool rai::block_store::account_get (MDB_txn * transaction_a, rai::account const & account_a, rai::account_info & info_a)
{
	rai::mdb_val value;
	auto status (mdb_get (transaction_a, accounts, rai::mdb_val (account_a), value));
	assert (status == 0 || status == MDB_NOTFOUND);
	bool result;
	if (status == MDB_NOTFOUND)
	{
		result = true;
	}
	else
	{
		rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		result = info_a.deserialize (stream);
		assert (!result);
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
		result = value.uint256 ();
	}
	return result;
}

void rai::block_store::frontier_del (MDB_txn * transaction_a, rai::block_hash const & block_a)
{
	auto status (mdb_del (transaction_a, frontiers, rai::mdb_val (block_a), nullptr));
	assert (status == 0);
}

size_t rai::block_store::frontier_count (MDB_txn * transaction_a)
{
	MDB_stat frontier_stats;
	auto status (mdb_stat (transaction_a, frontiers, &frontier_stats));
	assert (status == 0);
	auto result (frontier_stats.ms_entries);
	return result;
}

void rai::block_store::account_put (MDB_txn * transaction_a, rai::account const & account_a, rai::account_info const & info_a)
{
	auto status (mdb_put (transaction_a, accounts, rai::mdb_val (account_a), info_a.val (), 0));
	assert (status == 0);
}

void rai::block_store::pending_put (MDB_txn * transaction_a, rai::pending_key const & key_a, rai::pending_info const & pending_a)
{
	auto status (mdb_put (transaction_a, pending, key_a.val (), pending_a.val (), 0));
	assert (status == 0);
}

void rai::block_store::pending_del (MDB_txn * transaction_a, rai::pending_key const & key_a)
{
	auto status (mdb_del (transaction_a, pending, key_a.val (), nullptr));
	assert (status == 0);
}

bool rai::block_store::pending_exists (MDB_txn * transaction_a, rai::pending_key const & key_a)
{
	auto iterator (pending_begin (transaction_a, key_a));
	return iterator != rai::store_iterator (nullptr) && rai::pending_key (iterator->first) == key_a;
}

bool rai::block_store::pending_get (MDB_txn * transaction_a, rai::pending_key const & key_a, rai::pending_info & pending_a)
{
	rai::mdb_val value;
	auto status (mdb_get (transaction_a, pending, key_a.val (), value));
	assert (status == 0 || status == MDB_NOTFOUND);
	bool result;
	if (status == MDB_NOTFOUND)
	{
		result = true;
	}
	else
	{
		result = false;
		assert (value.size () == sizeof (pending_a.source.bytes) + sizeof (pending_a.amount.bytes));
		rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error1 (rai::read (stream, pending_a.source));
		assert (!error1);
		auto error2 (rai::read (stream, pending_a.amount));
		assert (!error2);
	}
	return result;
}

rai::store_iterator rai::block_store::pending_begin (MDB_txn * transaction_a, rai::pending_key const & key_a)
{
	rai::store_iterator result (transaction_a, pending, key_a.val ());
	return result;
}

rai::store_iterator rai::block_store::pending_begin (MDB_txn * transaction_a)
{
	rai::store_iterator result (transaction_a, pending);
	return result;
}

rai::store_iterator rai::block_store::pending_end ()
{
	rai::store_iterator result (nullptr);
	return result;
}

rai::pending_info::pending_info () :
source (0),
amount (0)
{
}

rai::pending_info::pending_info (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (source) + sizeof (amount) == sizeof (*this), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

rai::pending_info::pending_info (rai::account const & source_a, rai::amount const & amount_a) :
source (source_a),
amount (amount_a)
{
}

void rai::pending_info::serialize (rai::stream & stream_a) const
{
	rai::write (stream_a, source.bytes);
	rai::write (stream_a, amount.bytes);
}

bool rai::pending_info::deserialize (rai::stream & stream_a)
{
	auto result (rai::read (stream_a, source.bytes));
	if (!result)
	{
		result = rai::read (stream_a, amount.bytes);
	}
	return result;
}

bool rai::pending_info::operator== (rai::pending_info const & other_a) const
{
	return source == other_a.source && amount == other_a.amount;
}

rai::mdb_val rai::pending_info::val () const
{
	return rai::mdb_val (sizeof (*this), const_cast<rai::pending_info *> (this));
}

rai::pending_key::pending_key (rai::account const & account_a, rai::block_hash const & hash_a) :
account (account_a),
hash (hash_a)
{
}

rai::pending_key::pending_key (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (account) + sizeof (hash) == sizeof (*this), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

void rai::pending_key::serialize (rai::stream & stream_a) const
{
	rai::write (stream_a, account.bytes);
	rai::write (stream_a, hash.bytes);
}

bool rai::pending_key::deserialize (rai::stream & stream_a)
{
	auto error (rai::read (stream_a, account.bytes));
	if (!error)
	{
		error = rai::read (stream_a, hash.bytes);
	}
	return error;
}

bool rai::pending_key::operator== (rai::pending_key const & other_a) const
{
	return account == other_a.account && hash == other_a.hash;
}

rai::mdb_val rai::pending_key::val () const
{
	return rai::mdb_val (sizeof (*this), const_cast<rai::pending_key *> (this));
}

void rai::block_store::block_info_put (MDB_txn * transaction_a, rai::block_hash const & hash_a, rai::block_info const & block_info_a)
{
	auto status (mdb_put (transaction_a, blocks_info, rai::mdb_val (hash_a), block_info_a.val (), 0));
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
	return iterator != rai::store_iterator (nullptr) && rai::block_hash (iterator->first.uint256 ()) == hash_a;
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

rai::store_iterator rai::block_store::block_info_begin (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	rai::store_iterator result (transaction_a, blocks_info, rai::mdb_val (hash_a));
	return result;
}

rai::store_iterator rai::block_store::block_info_begin (MDB_txn * transaction_a)
{
	rai::store_iterator result (transaction_a, blocks_info);
	return result;
}

rai::store_iterator rai::block_store::block_info_end ()
{
	rai::store_iterator result (nullptr);
	return result;
}

rai::block_info::block_info () :
account (0),
balance (0)
{
}

rai::block_info::block_info (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (account) + sizeof (balance) == sizeof (*this), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

rai::block_info::block_info (rai::account const & account_a, rai::amount const & balance_a) :
account (account_a),
balance (balance_a)
{
}

void rai::block_info::serialize (rai::stream & stream_a) const
{
	rai::write (stream_a, account.bytes);
	rai::write (stream_a, balance.bytes);
}

bool rai::block_info::deserialize (rai::stream & stream_a)
{
	auto error (rai::read (stream_a, account.bytes));
	if (!error)
	{
		error = rai::read (stream_a, balance.bytes);
	}
	return error;
}

bool rai::block_info::operator== (rai::block_info const & other_a) const
{
	return account == other_a.account && balance == other_a.balance;
}

rai::mdb_val rai::block_info::val () const
{
	return rai::mdb_val (sizeof (*this), const_cast<rai::block_info *> (this));
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

rai::store_iterator rai::block_store::representation_begin (MDB_txn * transaction_a)
{
	rai::store_iterator result (transaction_a, representation);
	return result;
}

rai::store_iterator rai::block_store::representation_end ()
{
	rai::store_iterator result (nullptr);
	return result;
}

void rai::block_store::unchecked_clear (MDB_txn * transaction_a)
{
	auto status (mdb_drop (transaction_a, unchecked, 0));
	assert (status == 0);
}

void rai::block_store::unchecked_put (MDB_txn * transaction_a, rai::block_hash const & hash_a, std::shared_ptr<rai::block> const & block_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	unchecked_cache.insert (std::make_pair (hash_a, block_a));
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
	for (auto i (unchecked_begin (transaction_a, hash_a)), n (unchecked_end ()); i != n && rai::block_hash (i->first.uint256 ()) == hash_a; i.next_dup ())
	{
		rai::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
		result.push_back (rai::deserialize_block (stream));
	}
	return result;
}

void rai::block_store::unchecked_del (MDB_txn * transaction_a, rai::block_hash const & hash_a, rai::block const & block_a)
{
	{
		std::lock_guard<std::mutex> lock (cache_mutex);
		for (auto i (unchecked_cache.find (hash_a)), n (unchecked_cache.end ()); i != n && i->first == hash_a;)
		{
			if (*i->second == block_a)
			{
				i = unchecked_cache.erase (i);
			}
			else
			{
				++i;
			}
		}
	}
	std::vector<uint8_t> vector;
	{
		rai::vectorstream stream (vector);
		rai::serialize_block (stream, block_a);
	}
	auto status (mdb_del (transaction_a, unchecked, rai::mdb_val (hash_a), rai::mdb_val (vector.size (), vector.data ())));
	assert (status == 0 || status == MDB_NOTFOUND);
}

rai::store_iterator rai::block_store::unchecked_begin (MDB_txn * transaction_a)
{
	rai::store_iterator result (transaction_a, unchecked);
	return result;
}

rai::store_iterator rai::block_store::unchecked_begin (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	rai::store_iterator result (transaction_a, unchecked, rai::mdb_val (hash_a));
	return result;
}

rai::store_iterator rai::block_store::unchecked_end ()
{
	rai::store_iterator result (nullptr);
	return result;
}

size_t rai::block_store::unchecked_count (MDB_txn * transaction_a)
{
	MDB_stat unchecked_stats;
	auto status (mdb_stat (transaction_a, unchecked, &unchecked_stats));
	assert (status == 0);
	auto result (unchecked_stats.ms_entries);
	return result;
}

void rai::block_store::unsynced_put (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	auto status (mdb_put (transaction_a, unsynced, rai::mdb_val (hash_a), rai::mdb_val (0, nullptr), 0));
	assert (status == 0);
}

void rai::block_store::unsynced_del (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	auto status (mdb_del (transaction_a, unsynced, rai::mdb_val (hash_a), nullptr));
	assert (status == 0);
}

bool rai::block_store::unsynced_exists (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	auto iterator (unsynced_begin (transaction_a, hash_a));
	return iterator != rai::store_iterator (nullptr) && rai::block_hash (iterator->first.uint256 ()) == hash_a;
}

rai::store_iterator rai::block_store::unsynced_begin (MDB_txn * transaction_a)
{
	return rai::store_iterator (transaction_a, unsynced);
}

rai::store_iterator rai::block_store::unsynced_begin (MDB_txn * transaction_a, rai::uint256_union const & val_a)
{
	return rai::store_iterator (transaction_a, unsynced, rai::mdb_val (val_a));
}

rai::store_iterator rai::block_store::unsynced_end ()
{
	return rai::store_iterator (nullptr);
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

namespace
{
class hash2_visitor : public rai::block_visitor
{
public:
	hash2_visitor (rai::block_store & store_a, MDB_txn * transaction_a) :
	store (store_a),
	transaction (transaction_a),
	result (0)
	{
	}
	void send_block (rai::send_block const & block_a)
	{
		blake2b_state hash_l;
		auto status (blake2b_init (&hash_l, result.bytes.size ()));
		assert (status == 0);
		rai::uint256_union preamble (1);
		blake2b_update (&hash_l, preamble.bytes.data (), preamble.bytes.size ());

		rai::block_hash previous (store.hash2_get (transaction, block_a.hashables.previous));
		if (!previous.is_zero ())
		{
			status = blake2b_update (&hash_l, previous.bytes.data (), previous.bytes.size ());
			assert (status == 0);
			status = blake2b_update (&hash_l, block_a.hashables.destination.bytes.data (), block_a.hashables.destination.bytes.size ());
			assert (status == 0);
			status = blake2b_update (&hash_l, block_a.hashables.balance.bytes.data (), block_a.hashables.balance.bytes.size ());
			assert (status == 0);

			status = blake2b_final (&hash_l, result.bytes.data (), result.bytes.size ());
			assert (status == 0);
		}
	}
	void receive_block (rai::receive_block const & block_a)
	{
		blake2b_state hash_l;
		auto status (blake2b_init (&hash_l, result.bytes.size ()));
		assert (status == 0);
		rai::uint256_union preamble (2);
		blake2b_update (&hash_l, preamble.bytes.data (), preamble.bytes.size ());

		rai::block_hash previous (store.hash2_get (transaction, block_a.hashables.previous));
		if (!previous.is_zero ())
		{
			status = blake2b_update (&hash_l, previous.bytes.data (), previous.bytes.size ());
			assert (status == 0);
			rai::block_hash source (store.hash2_get (transaction, block_a.hashables.source));
			if (!source.is_zero ())
			{
				status = blake2b_update (&hash_l, source.bytes.data (), source.bytes.size ());
				assert (status == 0);

				status = blake2b_final (&hash_l, result.bytes.data (), result.bytes.size ());
				assert (status == 0);
			}
		}
	}
	void open_block (rai::open_block const & block_a)
	{
		blake2b_state hash_l;
		auto status (blake2b_init (&hash_l, result.bytes.size ()));
		assert (status == 0);
		rai::uint256_union preamble (3);
		blake2b_update (&hash_l, preamble.bytes.data (), preamble.bytes.size ());

		rai::block_hash source (store.hash2_get (transaction, block_a.hashables.source));
		if (!source.is_zero ())
		{
			status = blake2b_update (&hash_l, source.bytes.data (), source.bytes.size ());
			assert (status == 0);
			status = blake2b_update (&hash_l, block_a.hashables.representative.bytes.data (), block_a.hashables.representative.bytes.size ());
			assert (status == 0);
			status = blake2b_update (&hash_l, block_a.hashables.account.bytes.data (), block_a.hashables.account.bytes.size ());
			assert (status == 0);

			status = blake2b_final (&hash_l, result.bytes.data (), result.bytes.size ());
			assert (status == 0);
		}
	}
	void change_block (rai::change_block const & block_a)
	{
		blake2b_state hash_l;
		auto status (blake2b_init (&hash_l, result.bytes.size ()));
		assert (status == 0);
		rai::uint256_union preamble (4);
		blake2b_update (&hash_l, preamble.bytes.data (), preamble.bytes.size ());

		rai::block_hash previous (store.hash2_get (transaction, block_a.hashables.previous));
		if (!previous.is_zero ())
		{
			status = blake2b_update (&hash_l, previous.bytes.data (), previous.bytes.size ());
			assert (status == 0);
			status = blake2b_update (&hash_l, block_a.hashables.representative.bytes.data (), block_a.hashables.representative.bytes.size ());
			assert (status == 0);

			status = blake2b_final (&hash_l, result.bytes.data (), result.bytes.size ());
			assert (status == 0);
		}
	}
	rai::block_store & store;
	MDB_txn * transaction;
	rai::block_hash result;
};
}

rai::block_hash rai::block_store::hash2_calc (MDB_txn * transaction_a, rai::block const & block_a)
{
	hash2_visitor visitor (*this, transaction_a);
	block_a.visit (visitor);
	return visitor.result;
}

rai::block_hash rai::block_store::hash2_get (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	rai::block_hash result;
	for (auto & i : result.qwords)
	{
		i = 0;
	}
	rai::mdb_val value;
	auto status (mdb_get (transaction_a, hash2, rai::mdb_val (hash_a), value));
	assert (status == 0 || status == MDB_NOTFOUND);
	if (status == 0)
	{
		result = value.uint256 ();
	}
	return result;
}

void rai::block_store::hash2_put (MDB_txn * transaction_a, rai::block_hash const & hash_a, rai::block_hash const & hash2_a)
{
	assert (!hash2_a.is_zero ());
	auto status (mdb_put (transaction_a, hash2, rai::mdb_val (hash_a), rai::mdb_val (hash2_a), 0));
	assert (status == 0);
}

void rai::block_store::hash2_del (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	auto status (mdb_del (transaction_a, hash2, mdb_val (hash_a), nullptr));
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
		std::vector<uint8_t> vector;
		{
			rai::vectorstream stream (vector);
			rai::serialize_block (stream, *i.second);
		}
		auto status (mdb_put (transaction_a, unchecked, rai::mdb_val (i.first), rai::mdb_val (vector.size (), vector.data ()), 0));
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

rai::store_iterator rai::block_store::vote_begin (MDB_txn * transaction_a)
{
	return rai::store_iterator (transaction_a, vote);
}

rai::store_iterator rai::block_store::vote_end ()
{
	return rai::store_iterator (nullptr);
}

std::shared_ptr<rai::vote> rai::block_store::vote_get (MDB_txn * transaction_a, rai::account const & account_a)
{
	std::shared_ptr<rai::vote> result;
	rai::mdb_val value;
	auto status (mdb_get (transaction_a, vote, rai::mdb_val (account_a), value));
	assert (status == 0 || status == MDB_NOTFOUND);
	if (status == 0)
	{
		result = std::make_shared<rai::vote> (value);
		assert (result != nullptr);
	}
	return result;
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

std::pair<std::shared_ptr<rai::vote>, std::shared_ptr<rai::vote>> rai::block_store::vote_generate (MDB_txn * transaction_a, rai::account const & account_a, rai::raw_key const & key_a, std::shared_ptr<rai::block> block_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto current (vote_current (transaction_a, account_a));
	uint64_t sequence (current ? current->sequence : 0);
	std::pair<std::shared_ptr<rai::vote>, std::shared_ptr<rai::vote>> result;
	result.first = std::make_shared<rai::vote> (account_a, key_a, sequence + 1, block_a);
	vote_cache[account_a] = result.first;
	auto hash2 (hash2_calc (transaction_a, *block_a));
	result.second = std::make_shared<rai::vote> (account_a, key_a, sequence + 2, block_a, hash2);
	vote_cache[account_a] = result.second;
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

rai::vote_result rai::block_store::vote_validate (MDB_txn * transaction_a, std::shared_ptr<rai::vote> vote_a)
{
	rai::vote_result result ({ rai::vote_code::invalid, 0 });
	// Reject unsigned votes
	bool hash1_error (true);
	bool hash2_error (true);
	auto hash2_val (hash2_calc (transaction_a, *vote_a->block));
	if (!hash2_val.is_zero ())
	{
		hash2_error = rai::validate_message (vote_a->account, hash2_val, vote_a->signature);
	}
	hash1_error = rai::validate_message (vote_a->account, vote_a->hash (), vote_a->signature);
	if (!hash1_error || !hash2_error)
	{
		result.code = rai::vote_code::replay;
		result.vote = vote_max (transaction_a, vote_a); // Make sure this sequence number is > any we've seen from this account before
		if (result.vote == vote_a)
		{
			result.code = hash1_error ? rai::vote_code::vote2 : rai::vote_code::vote;
		}
	}
	return result;
}

bool rai::vote::operator== (rai::vote const & other_a) const
{
	return sequence == other_a.sequence && *block == *other_a.block && account == other_a.account && signature == other_a.signature;
}

bool rai::vote::operator!= (rai::vote const & other_a) const
{
	return !(*this == other_a);
}

std::string rai::vote::to_json () const
{
	std::stringstream stream;
	boost::property_tree::ptree tree;
	tree.put ("account", account.to_account ());
	tree.put ("signature", signature.number ());
	tree.put ("sequence", std::to_string (sequence));
	tree.put ("block", block->to_json ());
	boost::property_tree::write_json (stream, tree);
	return stream.str ();
}

namespace
{
class root_visitor : public rai::block_visitor
{
public:
	root_visitor (rai::block_store & store_a) :
	store (store_a)
	{
	}
	virtual ~root_visitor () = default;
	void send_block (rai::send_block const & block_a) override
	{
		result = block_a.previous ();
	}
	void receive_block (rai::receive_block const & block_a) override
	{
		result = block_a.previous ();
	}
	// Open blocks have no previous () so we use the account number
	void open_block (rai::open_block const & block_a) override
	{
		rai::transaction transaction (store.environment, nullptr, false);
		auto hash (block_a.source ());
		auto source (store.block_get (transaction, hash));
		if (source != nullptr)
		{
			auto send (dynamic_cast<rai::send_block *> (source.get ()));
			if (send != nullptr)
			{
				result = send->hashables.destination;
			}
			else
			{
				result.clear ();
			}
		}
		else
		{
			result.clear ();
		}
	}
	void change_block (rai::change_block const & block_a) override
	{
		result = block_a.previous ();
	}
	rai::block_store & store;
	rai::block_hash result;
};
}

rai::store_iterator rai::block_store::latest_begin (MDB_txn * transaction_a, rai::account const & account_a)
{
	rai::store_iterator result (transaction_a, accounts, rai::mdb_val (account_a));
	return result;
}

rai::store_iterator rai::block_store::latest_begin (MDB_txn * transaction_a)
{
	rai::store_iterator result (transaction_a, accounts);
	return result;
}

rai::store_iterator rai::block_store::latest_end ()
{
	rai::store_iterator result (nullptr);
	return result;
}

namespace
{
class ledger_processor : public rai::block_visitor
{
public:
	ledger_processor (rai::ledger &, MDB_txn *);
	virtual ~ledger_processor () = default;
	void send_block (rai::send_block const &) override;
	void receive_block (rai::receive_block const &) override;
	void open_block (rai::open_block const &) override;
	void change_block (rai::change_block const &) override;
	rai::ledger & ledger;
	MDB_txn * transaction;
	rai::process_return result;
};

// Determine the amount delta resultant from this block
class amount_visitor : public rai::block_visitor
{
public:
	amount_visitor (MDB_txn *, rai::block_store &);
	virtual ~amount_visitor () = default;
	void compute (rai::block_hash const &);
	void send_block (rai::send_block const &) override;
	void receive_block (rai::receive_block const &) override;
	void open_block (rai::open_block const &) override;
	void change_block (rai::change_block const &) override;
	void from_send (rai::block_hash const &);
	MDB_txn * transaction;
	rai::block_store & store;
	rai::uint128_t result;
};

// Determine the balance as of this block
class balance_visitor : public rai::block_visitor
{
public:
	balance_visitor (MDB_txn *, rai::block_store &);
	virtual ~balance_visitor () = default;
	void compute (rai::block_hash const &);
	void send_block (rai::send_block const &) override;
	void receive_block (rai::receive_block const &) override;
	void open_block (rai::open_block const &) override;
	void change_block (rai::change_block const &) override;
	MDB_txn * transaction;
	rai::block_store & store;
	rai::block_hash current;
	rai::uint128_t result;
};

amount_visitor::amount_visitor (MDB_txn * transaction_a, rai::block_store & store_a) :
transaction (transaction_a),
store (store_a)
{
}

void amount_visitor::send_block (rai::send_block const & block_a)
{
	balance_visitor prev (transaction, store);
	prev.compute (block_a.hashables.previous);
	result = prev.result - block_a.hashables.balance.number ();
}

void amount_visitor::receive_block (rai::receive_block const & block_a)
{
	from_send (block_a.hashables.source);
}

void amount_visitor::open_block (rai::open_block const & block_a)
{
	if (block_a.hashables.source != rai::genesis_account)
	{
		from_send (block_a.hashables.source);
	}
	else
	{
		result = rai::genesis_amount;
	}
}

void amount_visitor::change_block (rai::change_block const & block_a)
{
	result = 0;
}

void amount_visitor::from_send (rai::block_hash const & hash_a)
{
	auto source_block (store.block_get (transaction, hash_a));
	assert (source_block != nullptr);
	source_block->visit (*this);
}

balance_visitor::balance_visitor (MDB_txn * transaction_a, rai::block_store & store_a) :
transaction (transaction_a),
store (store_a),
current (0),
result (0)
{
}

void balance_visitor::send_block (rai::send_block const & block_a)
{
	result += block_a.hashables.balance.number ();
	current = 0;
}

void balance_visitor::receive_block (rai::receive_block const & block_a)
{
	amount_visitor source (transaction, store);
	source.compute (block_a.hashables.source);
	rai::block_info block_info;
	if (!store.block_info_get (transaction, block_a.hash (), block_info))
	{
		result += block_info.balance.number ();
		current = 0;
	}
	else
	{
		result += source.result;
		current = block_a.hashables.previous;
	}
}

void balance_visitor::open_block (rai::open_block const & block_a)
{
	amount_visitor source (transaction, store);
	source.compute (block_a.hashables.source);
	result += source.result;
	current = 0;
}

void balance_visitor::change_block (rai::change_block const & block_a)
{
	rai::block_info block_info;
	if (!store.block_info_get (transaction, block_a.hash (), block_info))
	{
		result += block_info.balance.number ();
		current = 0;
	}
	else
	{
		current = block_a.hashables.previous;
	}
}

// Rollback this block
class rollback_visitor : public rai::block_visitor
{
public:
	rollback_visitor (MDB_txn * transaction_a, rai::ledger & ledger_a) :
	transaction (transaction_a),
	ledger (ledger_a)
	{
	}
	virtual ~rollback_visitor () = default;
	void send_block (rai::send_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto hash2 (ledger.store.hash2_get (transaction, hash));
		assert (!hash2.is_zero ());
		rai::pending_info pending;
		rai::pending_key key (block_a.hashables.destination, hash);
		while (ledger.store.pending_get (transaction, key, pending))
		{
			ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.destination));
		}
		rai::account_info info;
		auto error (ledger.store.account_get (transaction, pending.source, info));
		assert (!error);
		ledger.store.pending_del (transaction, key);
		ledger.store.representation_add (transaction, ledger.representative (transaction, hash), pending.amount.number ());
		ledger.change_latest (transaction, pending.source, block_a.hashables.previous, info.rep_block, ledger.balance (transaction, block_a.hashables.previous), info.block_count - 1);
		ledger.store.hash2_del (transaction, hash);
		ledger.store.block_del (transaction, hash2);
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, pending.source);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		if (!(info.block_count % ledger.store.block_info_max))
		{
			ledger.store.block_info_del (transaction, hash);
		}
	}
	void receive_block (rai::receive_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto hash2 (ledger.store.hash2_get (transaction, hash));
		assert (!hash2.is_zero ());
		auto representative (ledger.representative (transaction, block_a.hashables.previous));
		auto amount (ledger.amount (transaction, block_a.hashables.source));
		auto destination_account (ledger.account (transaction, hash));
		rai::account_info info;
		auto error (ledger.store.account_get (transaction, destination_account, info));
		assert (!error);
		ledger.store.representation_add (transaction, ledger.representative (transaction, hash), 0 - amount);
		ledger.change_latest (transaction, destination_account, block_a.hashables.previous, representative, ledger.balance (transaction, block_a.hashables.previous), info.block_count - 1);
		ledger.store.block_del (transaction, hash2);
		ledger.store.hash2_del (transaction, hash);
		ledger.store.pending_put (transaction, rai::pending_key (destination_account, block_a.hashables.source), { ledger.account (transaction, block_a.hashables.source), amount });
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, destination_account);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		if (!(info.block_count % ledger.store.block_info_max))
		{
			ledger.store.block_info_del (transaction, hash);
		}
	}
	void open_block (rai::open_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto hash2 (ledger.store.hash2_get (transaction, hash));
		assert (!hash2.is_zero ());
		auto amount (ledger.amount (transaction, block_a.hashables.source));
		auto destination_account (ledger.account (transaction, hash));
		ledger.store.representation_add (transaction, ledger.representative (transaction, hash), 0 - amount);
		ledger.change_latest (transaction, destination_account, 0, 0, 0, 0);
		ledger.store.block_del (transaction, hash2);
		ledger.store.hash2_del (transaction, hash);
		ledger.store.pending_put (transaction, rai::pending_key (destination_account, block_a.hashables.source), { ledger.account (transaction, block_a.hashables.source), amount });
		ledger.store.frontier_del (transaction, hash);
	}
	void change_block (rai::change_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto hash2 (ledger.store.hash2_get (transaction, hash));
		auto representative (ledger.representative (transaction, block_a.hashables.previous));
		auto account (ledger.account (transaction, block_a.hashables.previous));
		rai::account_info info;
		auto error (ledger.store.account_get (transaction, account, info));
		assert (!error);
		auto balance (ledger.balance (transaction, block_a.hashables.previous));
		ledger.store.representation_add (transaction, representative, balance);
		ledger.store.representation_add (transaction, hash, 0 - balance);
		ledger.store.block_del (transaction, hash2);
		ledger.store.hash2_del (transaction, hash);
		ledger.change_latest (transaction, account, block_a.hashables.previous, representative, info.balance, info.block_count - 1);
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, account);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		if (!(info.block_count % ledger.store.block_info_max))
		{
			ledger.store.block_info_del (transaction, hash);
		}
	}
	MDB_txn * transaction;
	rai::ledger & ledger;
};
}

void amount_visitor::compute (rai::block_hash const & block_hash)
{
	auto block (store.block_get (transaction, block_hash));
	if (block != nullptr)
	{
		block->visit (*this);
	}
	else
	{
		if (block_hash == rai::genesis_account)
		{
			result = std::numeric_limits<rai::uint128_t>::max ();
		}
		else
		{
			assert (false);
			result = 0;
		}
	}
}

void balance_visitor::compute (rai::block_hash const & block_hash)
{
	current = block_hash;
	while (!current.is_zero ())
	{
		auto block (store.block_get (transaction, current));
		assert (block != nullptr);
		block->visit (*this);
	}
}

// Balance for account containing hash
rai::uint128_t rai::ledger::balance (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	balance_visitor visitor (transaction_a, store);
	visitor.compute (hash_a);
	return visitor.result;
}

rai::uint128_t rai::block_store::block_balance (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	balance_visitor visitor (transaction_a, *this);
	visitor.compute (hash_a);
	return visitor.result;
}

// Balance for an account by account number
rai::uint128_t rai::ledger::account_balance (MDB_txn * transaction_a, rai::account const & account_a)
{
	rai::uint128_t result (0);
	rai::account_info info;
	auto none (store.account_get (transaction_a, account_a, info));
	if (!none)
	{
		result = info.balance.number ();
	}
	return result;
}

rai::uint128_t rai::ledger::account_pending (MDB_txn * transaction_a, rai::account const & account_a)
{
	rai::uint128_t result (0);
	rai::account end (account_a.number () + 1);
	for (auto i (store.pending_begin (transaction_a, rai::pending_key (account_a, 0))), n (store.pending_begin (transaction_a, rai::pending_key (end, 0))); i != n; ++i)
	{
		rai::pending_info info (i->second);
		result += info.amount.number ();
	}
	return result;
}

rai::process_return rai::ledger::process (MDB_txn * transaction_a, rai::block const & block_a)
{
	ledger_processor processor (*this, transaction_a);
	block_a.visit (processor);
	return processor.result;
}

// Money supply for heuristically calculating vote percentages
rai::uint128_t rai::ledger::supply (MDB_txn * transaction_a)
{
	auto unallocated (account_balance (transaction_a, rai::genesis_account));
	auto burned (account_pending (transaction_a, 0));
	auto absolute_supply (rai::genesis_amount - unallocated - burned);
	auto adjusted_supply (absolute_supply - inactive_supply);
	return adjusted_supply <= absolute_supply ? adjusted_supply : 0;
}

rai::block_hash rai::ledger::representative (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	auto result (representative_calculated (transaction_a, hash_a));
	assert (result.is_zero () || store.block_exists (transaction_a, result));
	return result;
}

rai::block_hash rai::ledger::representative_calculated (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	representative_visitor visitor (transaction_a, store);
	visitor.compute (hash_a);
	return visitor.result;
}

bool rai::ledger::block_exists (rai::block_hash const & hash_a)
{
	rai::transaction transaction (store.environment, nullptr, false);
	auto result (store.block_exists (transaction, hash_a));
	return result;
}

std::string rai::ledger::block_text (char const * hash_a)
{
	return block_text (rai::block_hash (hash_a));
}

std::string rai::ledger::block_text (rai::block_hash const & hash_a)
{
	std::string result;
	rai::transaction transaction (store.environment, nullptr, false);
	auto block (store.block_get (transaction, hash_a));
	if (block != nullptr)
	{
		block->serialize_json (result);
	}
	return result;
}

// Vote weight of an account
rai::uint128_t rai::ledger::weight (MDB_txn * transaction_a, rai::account const & account_a)
{
	if (check_bootstrap_weights.load ())
	{
		auto blocks = store.block_count (transaction_a);
		if (blocks.sum () < bootstrap_weight_max_blocks)
		{
			auto weight = bootstrap_weights.find (account_a);
			if (weight != bootstrap_weights.end ())
			{
				return weight->second;
			}
		}
		else
		{
			check_bootstrap_weights = false;
		}
	}
	return store.representation_get (transaction_a, account_a);
}

// Rollback blocks until `block_a' doesn't exist
void rai::ledger::rollback (MDB_txn * transaction_a, rai::block_hash const & block_a)
{
	assert (store.block_exists (transaction_a, block_a));
	auto account_l (account (transaction_a, block_a));
	rollback_visitor rollback (transaction_a, *this);
	rai::account_info info;
	while (store.block_exists (transaction_a, block_a))
	{
		auto latest_error (store.account_get (transaction_a, account_l, info));
		assert (!latest_error);
		auto block (store.block_get (transaction_a, info.head));
		block->visit (rollback);
	}
}

// Return account containing hash
rai::account rai::ledger::account (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	assert (store.block_exists (transaction_a, hash_a));
	auto hash (hash_a);
	rai::block_hash successor (1);
	rai::block_info block_info;
	while (!successor.is_zero () && store.block_info_get (transaction_a, successor, block_info))
	{
		successor = store.block_successor (transaction_a, hash);
		if (!successor.is_zero ())
		{
			hash = successor;
		}
	}
	rai::account result;
	if (successor.is_zero ())
	{
		result = store.frontier_get (transaction_a, hash);
	}
	else
	{
		result = block_info.account;
	}
	assert (!result.is_zero ());
	return result;
}

// Return amount decrease or increase for block
rai::uint128_t rai::ledger::amount (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	amount_visitor amount (transaction_a, store);
	amount.compute (hash_a);
	return amount.result;
}

void rai::block_store::representation_add (MDB_txn * transaction_a, rai::block_hash const & source_a, rai::uint128_t const & amount_a)
{
	auto source_block (block_get (transaction_a, source_a));
	assert (source_block != nullptr);
	auto source_rep (source_block->representative ());
	auto source_previous (representation_get (transaction_a, source_rep));
	representation_put (transaction_a, source_rep, source_previous + amount_a);
}

// Return latest block for account
rai::block_hash rai::ledger::latest (MDB_txn * transaction_a, rai::account const & account_a)
{
	rai::account_info info;
	auto latest_error (store.account_get (transaction_a, account_a, info));
	return latest_error ? 0 : info.head;
}

// Return latest root for account, account number of there are no blocks for this account.
rai::block_hash rai::ledger::latest_root (MDB_txn * transaction_a, rai::account const & account_a)
{
	rai::account_info info;
	auto latest_error (store.account_get (transaction_a, account_a, info));
	rai::block_hash result;
	if (latest_error)
	{
		result = account_a;
	}
	else
	{
		result = info.head;
	}
	return result;
}

rai::checksum rai::ledger::checksum (MDB_txn * transaction_a, rai::account const & begin_a, rai::account const & end_a)
{
	rai::checksum result;
	auto error (store.checksum_get (transaction_a, 0, 0, result));
	assert (!error);
	return result;
}

void rai::ledger::dump_account_chain (rai::account const & account_a)
{
	rai::transaction transaction (store.environment, nullptr, false);
	auto hash (latest (transaction, account_a));
	while (!hash.is_zero ())
	{
		auto block (store.block_get (transaction, hash));
		assert (block != nullptr);
		std::cerr << hash.to_string () << std::endl;
		hash = block->previous ();
	}
}

void rai::ledger::checksum_update (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	rai::checksum value;
	auto error (store.checksum_get (transaction_a, 0, 0, value));
	assert (!error);
	value ^= hash_a;
	store.checksum_put (transaction_a, 0, 0, value);
}

void rai::ledger::change_latest (MDB_txn * transaction_a, rai::account const & account_a, rai::block_hash const & hash_a, rai::block_hash const & rep_block_a, rai::amount const & balance_a, uint64_t block_count_a)
{
	rai::account_info info;
	auto exists (!store.account_get (transaction_a, account_a, info));
	if (exists)
	{
		checksum_update (transaction_a, info.head);
	}
	else
	{
		assert (dynamic_cast<rai::open_block *> (store.block_get (transaction_a, hash_a).get ()) != nullptr);
		info.open_block = hash_a;
	}
	if (!hash_a.is_zero ())
	{
		info.head = hash_a;
		info.rep_block = rep_block_a;
		info.balance = balance_a;
		info.modified = rai::seconds_since_epoch ();
		info.block_count = block_count_a;
		store.account_put (transaction_a, account_a, info);
		if (!(block_count_a % store.block_info_max))
		{
			rai::block_info block_info;
			block_info.account = account_a;
			block_info.balance = balance_a;
			store.block_info_put (transaction_a, hash_a, block_info);
		}
		checksum_update (transaction_a, hash_a);
	}
	else
	{
		store.account_del (transaction_a, account_a);
	}
}

std::unique_ptr<rai::block> rai::ledger::successor (MDB_txn * transaction_a, rai::block_hash const & block_a)
{
	assert (store.account_exists (transaction_a, block_a) || store.block_exists (transaction_a, block_a));
	assert (store.account_exists (transaction_a, block_a) || latest (transaction_a, account (transaction_a, block_a)) != block_a);
	rai::block_hash successor;
	if (store.account_exists (transaction_a, block_a))
	{
		rai::account_info info;
		auto error (store.account_get (transaction_a, block_a, info));
		assert (!error);
		successor = info.open_block;
	}
	else
	{
		successor = store.block_successor (transaction_a, block_a);
	}
	assert (!successor.is_zero ());
	auto result (store.block_get (transaction_a, successor));
	assert (result != nullptr);
	return result;
}

std::unique_ptr<rai::block> rai::ledger::forked_block (MDB_txn * transaction_a, rai::block const & block_a)
{
	auto hash2 (store.hash2_calc (transaction_a, block_a));
	assert (!hash2.is_zero ());
	assert (!store.block_exists (transaction_a, hash2));
	auto root (block_a.root ());
	assert (store.block_exists (transaction_a, root) || store.account_exists (transaction_a, root));
	std::unique_ptr<rai::block> result (store.block_get (transaction_a, store.block_successor (transaction_a, root)));
	if (result == nullptr)
	{
		rai::account_info info;
		auto error (store.account_get (transaction_a, root, info));
		assert (!error);
		result = store.block_get (transaction_a, info.open_block);
		assert (result != nullptr);
	}
	return result;
}

void ledger_processor::change_block (rai::change_block const & block_a)
{
	auto hash (block_a.hash ());
	auto hash2 (ledger.store.hash2_calc (transaction, block_a));
	auto existing (ledger.store.block_exists (transaction, hash2));
	result.code = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == rai::process_result::progress)
	{
		auto previous (ledger.store.block_exists (transaction, block_a.hashables.previous));
		result.code = previous ? rai::process_result::progress : rai::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == rai::process_result::progress)
		{
			auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
			result.code = account.is_zero () ? rai::process_result::fork : rai::process_result::progress;
			if (result.code == rai::process_result::progress)
			{
				rai::account_info info;
				auto latest_error (ledger.store.account_get (transaction, account, info));
				assert (!latest_error);
				assert (info.head == block_a.hashables.previous);
				result.code = validate_message (account, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is this block signed correctly (Malformed)
				if (result.code == rai::process_result::progress)
				{
					if (hash2.is_zero ())
					{
						// recalculate hash2 to prevent race condition
						hash2 = ledger.store.hash2_calc (transaction, block_a);
						assert (!hash2.is_zero ());
					}
					ledger.store.hash2_put (transaction, hash, hash2);
					ledger.store.block_put (transaction, hash, block_a);
					auto balance (ledger.balance (transaction, block_a.hashables.previous));
					ledger.store.representation_add (transaction, hash, balance);
					ledger.store.representation_add (transaction, info.rep_block, 0 - balance);
					ledger.change_latest (transaction, account, hash, hash, info.balance, info.block_count + 1);
					ledger.store.frontier_del (transaction, block_a.hashables.previous);
					ledger.store.frontier_put (transaction, hash, account);
					result.account = account;
					result.amount = 0;
				}
			}
		}
	}
}

void ledger_processor::send_block (rai::send_block const & block_a)
{
	auto hash (block_a.hash ());
	auto hash2 (ledger.store.hash2_calc (transaction, block_a));
	auto existing (ledger.store.block_exists (transaction, hash2));
	result.code = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == rai::process_result::progress)
	{
		auto previous (ledger.store.block_exists (transaction, block_a.hashables.previous));
		result.code = previous ? rai::process_result::progress : rai::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == rai::process_result::progress)
		{
			auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
			result.code = account.is_zero () ? rai::process_result::fork : rai::process_result::progress;
			if (result.code == rai::process_result::progress)
			{
				result.code = validate_message (account, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is this block signed correctly (Malformed)
				if (result.code == rai::process_result::progress)
				{
					rai::account_info info;
					auto latest_error (ledger.store.account_get (transaction, account, info));
					assert (!latest_error);
					assert (info.head == block_a.hashables.previous);
					result.code = info.balance.number () >= block_a.hashables.balance.number () ? rai::process_result::progress : rai::process_result::negative_spend; // Is this trying to spend a negative amount (Malicious)
					if (result.code == rai::process_result::progress)
					{
						if (hash2.is_zero ())
						{
							// recalculate hash2 to prevent race condition
							hash2 = ledger.store.hash2_calc (transaction, block_a);
							assert (!hash2.is_zero ());
						}
						ledger.store.hash2_put (transaction, hash, hash2);
						auto amount (info.balance.number () - block_a.hashables.balance.number ());
						ledger.store.representation_add (transaction, info.rep_block, 0 - amount);
						ledger.store.block_put (transaction, hash, block_a);
						ledger.change_latest (transaction, account, hash, info.rep_block, block_a.hashables.balance, info.block_count + 1);
						ledger.store.pending_put (transaction, rai::pending_key (block_a.hashables.destination, hash), { account, amount });
						ledger.store.frontier_del (transaction, block_a.hashables.previous);
						ledger.store.frontier_put (transaction, hash, account);
						result.account = account;
						result.amount = amount;
						result.pending_account = block_a.hashables.destination;
					}
				}
			}
		}
	}
}

void ledger_processor::receive_block (rai::receive_block const & block_a)
{
	auto hash (block_a.hash ());
	auto hash2 (ledger.store.hash2_calc (transaction, block_a));
	auto existing (ledger.store.block_exists (transaction, hash2));
	result.code = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block already?  (Harmless)
	if (result.code == rai::process_result::progress)
	{
		result.code = ledger.store.block_exists (transaction, block_a.hashables.source) ? rai::process_result::progress : rai::process_result::gap_source; // Have we seen the source block already? (Harmless)
		if (result.code == rai::process_result::progress)
		{
			auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
			result.code = account.is_zero () ? rai::process_result::gap_previous : rai::process_result::progress; //Have we seen the previous block? No entries for account at all (Harmless)
			if (result.code == rai::process_result::progress)
			{
				result.code = rai::validate_message (account, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is the signature valid (Malformed)
				if (result.code == rai::process_result::progress)
				{
					rai::account_info info;
					ledger.store.account_get (transaction, account, info);
					result.code = info.head == block_a.hashables.previous ? rai::process_result::progress : rai::process_result::gap_previous; // Block doesn't immediately follow latest block (Harmless)
					if (result.code == rai::process_result::progress)
					{
						rai::pending_key key (account, block_a.hashables.source);
						rai::pending_info pending;
						result.code = ledger.store.pending_get (transaction, key, pending) ? rai::process_result::unreceivable : rai::process_result::progress; // Has this source already been received (Malformed)
						if (result.code == rai::process_result::progress)
						{
							auto new_balance (info.balance.number () + pending.amount.number ());
							rai::account_info source_info;
							auto error (ledger.store.account_get (transaction, pending.source, source_info));
							assert (!error);
							if (hash2.is_zero ())
							{
								// recalculate hash2 to prevent race condition
								hash2 = ledger.store.hash2_calc (transaction, block_a);
								assert (!hash2.is_zero ());
							}
							ledger.store.hash2_put (transaction, hash, hash2);
							ledger.store.pending_del (transaction, key);
							ledger.store.block_put (transaction, hash, block_a);
							ledger.change_latest (transaction, account, hash, info.rep_block, new_balance, info.block_count + 1);
							ledger.store.representation_add (transaction, info.rep_block, pending.amount.number ());
							ledger.store.frontier_del (transaction, block_a.hashables.previous);
							ledger.store.frontier_put (transaction, hash, account);
							result.account = account;
							result.amount = pending.amount;
						}
					}
				}
			}
			else
			{
				result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? rai::process_result::fork : rai::process_result::gap_previous; // If we have the block but it's not the latest we have a signed fork (Malicious)
			}
		}
	}
}

void ledger_processor::open_block (rai::open_block const & block_a)
{
	auto hash (block_a.hash ());
	auto hash2 (ledger.store.hash2_calc (transaction, block_a));
	auto existing (ledger.store.block_exists (transaction, hash2));
	result.code = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block already? (Harmless)
	if (result.code == rai::process_result::progress)
	{
		auto source_missing (!ledger.store.block_exists (transaction, block_a.hashables.source));
		result.code = source_missing ? rai::process_result::gap_source : rai::process_result::progress; // Have we seen the source block? (Harmless)
		if (result.code == rai::process_result::progress)
		{
			result.code = rai::validate_message (block_a.hashables.account, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is the signature valid (Malformed)
			if (result.code == rai::process_result::progress)
			{
				rai::account_info info;
				result.code = ledger.store.account_get (transaction, block_a.hashables.account, info) ? rai::process_result::progress : rai::process_result::fork; // Has this account already been opened? (Malicious)
				if (result.code == rai::process_result::progress)
				{
					rai::pending_key key (block_a.hashables.account, block_a.hashables.source);
					rai::pending_info pending;
					result.code = ledger.store.pending_get (transaction, key, pending) ? rai::process_result::unreceivable : rai::process_result::progress; // Has this source already been received (Malformed)
					if (result.code == rai::process_result::progress)
					{
						result.code = block_a.hashables.account == rai::burn_account ? rai::process_result::opened_burn_account : rai::process_result::progress; // Is it burning 0 account? (Malicious)
						if (result.code == rai::process_result::progress)
						{
							rai::account_info source_info;
							auto error (ledger.store.account_get (transaction, pending.source, source_info));
							assert (!error);
							if (hash2.is_zero ())
							{
								// recalculate hash2 to prevent race condition
								hash2 = ledger.store.hash2_calc (transaction, block_a);
								assert (!hash2.is_zero ());
							}
							ledger.store.pending_del (transaction, key);
							ledger.store.hash2_put (transaction, hash, hash2);
							ledger.store.block_put (transaction, hash, block_a);
							ledger.change_latest (transaction, block_a.hashables.account, hash, hash, pending.amount.number (), info.block_count + 1);
							ledger.store.representation_add (transaction, hash, pending.amount.number ());
							ledger.store.frontier_put (transaction, hash, block_a.hashables.account);
							result.account = block_a.hashables.account;
							result.amount = pending.amount;
						}
					}
				}
			}
		}
	}
}

ledger_processor::ledger_processor (rai::ledger & ledger_a, MDB_txn * transaction_a) :
ledger (ledger_a),
transaction (transaction_a)
{
}

rai::vote::vote (rai::vote const & other_a) :
sequence (other_a.sequence),
block (other_a.block),
account (other_a.account),
signature (other_a.signature)
{
}

rai::vote::vote (bool & error_a, rai::stream & stream_a)
{
	if (!error_a)
	{
		error_a = rai::read (stream_a, account.bytes);
		if (!error_a)
		{
			error_a = rai::read (stream_a, signature.bytes);
			if (!error_a)
			{
				error_a = rai::read (stream_a, sequence);
				if (!error_a)
				{
					block = rai::deserialize_block (stream_a);
					error_a = block == nullptr;
				}
			}
		}
	}
}

rai::vote::vote (bool & error_a, rai::stream & stream_a, rai::block_type type_a)
{
	if (!error_a)
	{
		error_a = rai::read (stream_a, account.bytes);
		if (!error_a)
		{
			error_a = rai::read (stream_a, signature.bytes);
			if (!error_a)
			{
				error_a = rai::read (stream_a, sequence);
				if (!error_a)
				{
					block = rai::deserialize_block (stream_a, type_a);
					error_a = block == nullptr;
				}
			}
		}
	}
}

rai::vote::vote (rai::account const & account_a, rai::raw_key const & prv_a, uint64_t sequence_a, std::shared_ptr<rai::block> block_a) :
sequence (sequence_a),
block (block_a),
account (account_a),
signature (rai::sign_message (prv_a, account_a, hash ()))
{
}

rai::vote::vote (rai::account const & account_a, rai::raw_key const & prv_a, uint64_t sequence_a, std::shared_ptr<rai::block> block_a, rai::block_hash const & hash_a) :
sequence (sequence_a),
block (block_a),
account (account_a),
signature (rai::sign_message (prv_a, account_a, hash_a))
{
}

rai::vote::vote (MDB_val const & value_a)
{
	rai::bufferstream stream (reinterpret_cast<uint8_t const *> (value_a.mv_data), value_a.mv_size);
	auto error (rai::read (stream, account.bytes));
	assert (!error);
	error = rai::read (stream, signature.bytes);
	assert (!error);
	error = rai::read (stream, sequence);
	assert (!error);
	block = rai::deserialize_block (stream);
	assert (block != nullptr);
}

rai::uint256_union rai::vote::hash () const
{
	rai::uint256_union result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result.bytes));
	blake2b_update (&hash, block->hash ().bytes.data (), sizeof (result.bytes));
	union
	{
		uint64_t qword;
		std::array<uint8_t, 8> bytes;
	};
	qword = sequence;
	blake2b_update (&hash, bytes.data (), sizeof (bytes));
	blake2b_final (&hash, result.bytes.data (), sizeof (result.bytes));
	return result;
}

void rai::vote::serialize (rai::stream & stream_a, rai::block_type)
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, sequence);
	block->serialize (stream_a);
}

void rai::vote::serialize (rai::stream & stream_a)
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, sequence);
	rai::serialize_block (stream_a, *block);
}

rai::genesis::genesis ()
{
	boost::property_tree::ptree tree;
	std::stringstream istream (rai::genesis_block);
	boost::property_tree::read_json (istream, tree);
	auto block (rai::deserialize_block_json (tree));
	assert (dynamic_cast<rai::open_block *> (block.get ()) != nullptr);
	open.reset (static_cast<rai::open_block *> (block.release ()));
}

void rai::genesis::initialize (MDB_txn * transaction_a, rai::block_store & store_a) const
{
	auto hash_l (hash ());
	auto hash2 (store_a.hash2_calc (transaction_a, *open));
	assert (!hash2.is_zero ());
	assert (store_a.latest_begin (transaction_a) == store_a.latest_end ());
	store_a.hash2_put (transaction_a, hash_l, hash2);
	store_a.block_put (transaction_a, hash2, *open);
	store_a.account_put (transaction_a, genesis_account, { hash_l, open->hash (), open->hash (), std::numeric_limits<rai::uint128_t>::max (), rai::seconds_since_epoch (), 1 });
	store_a.representation_put (transaction_a, genesis_account, std::numeric_limits<rai::uint128_t>::max ());
	store_a.checksum_put (transaction_a, 0, 0, hash_l);
	store_a.frontier_put (transaction_a, hash_l, genesis_account);
}

rai::block_hash rai::genesis::hash () const
{
	return open->hash ();
}
