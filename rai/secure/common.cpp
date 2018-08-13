#include <rai/secure/common.hpp>

#include <rai/lib/interface.h>
#include <rai/node/common.hpp>
#include <rai/secure/blockstore.hpp>
#include <rai/secure/versioning.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <queue>

#include <ed25519-donna/ed25519.h>

// Genesis keys for network variants
namespace
{
char const * test_private_key_data = "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4";
char const * test_public_key_data = "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0"; // xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo
char const * beta_public_key_data = "A59A47CC4F593E75AE9AD653FDA9358E2F7898D9ACC8C60E80D0495CE20FBA9F"; // xrb_3betaz86ypbygpqbookmzpnmd5jhh4efmd8arr9a3n4bdmj1zgnzad7xpmfp
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
        "source": "A59A47CC4F593E75AE9AD653FDA9358E2F7898D9ACC8C60E80D0495CE20FBA9F",
        "representative": "xrb_3betaz86ypbygpqbookmzpnmd5jhh4efmd8arr9a3n4bdmj1zgnzad7xpmfp",
        "account": "xrb_3betaz86ypbygpqbookmzpnmd5jhh4efmd8arr9a3n4bdmj1zgnzad7xpmfp",
        "work": "000000000f0aaeeb",
        "signature": "A726490E3325E4FA59C1C900D5B6EEBB15FE13D99F49D475B93F0AACC5635929A0614CF3892764A04D1C6732A0D716FFEB254D4154C6F544D11E6630F201450B"
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
		// Randomly generating these mean no two nodes will ever have the same sentinel values which protects against some insecure algorithms
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
size_t constexpr rai::state_block::size;

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

// Create a new random keypair
rai::keypair::keypair ()
{
	random_pool.GenerateBlock (prv.data.bytes.data (), prv.data.bytes.size ());
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a private key
rai::keypair::keypair (rai::raw_key && prv_a) :
prv (std::move (prv_a))
{
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a hex string of the private key
rai::keypair::keypair (std::string const & prv_a)
{
	auto error (prv.data.decode_hex (prv_a));
	assert (!error);
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Serialize a block prefixed with an 8-bit typecode
void rai::serialize_block (rai::stream & stream_a, rai::block const & block_a)
{
	write (stream_a, block_a.type ());
	block_a.serialize (stream_a);
}

rai::account_info::account_info () :
head (0),
rep_block (0),
open_block (0),
balance (0),
modified (0),
block_count (0),
epoch (rai::epoch::epoch_0)
{
}

rai::account_info::account_info (rai::block_hash const & head_a, rai::block_hash const & rep_block_a, rai::block_hash const & open_block_a, rai::amount const & balance_a, uint64_t modified_a, uint64_t block_count_a, rai::epoch epoch_a) :
head (head_a),
rep_block (rep_block_a),
open_block (open_block_a),
balance (balance_a),
modified (modified_a),
block_count (block_count_a),
epoch (epoch_a)
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
	return head == other_a.head && rep_block == other_a.rep_block && open_block == other_a.open_block && balance == other_a.balance && modified == other_a.modified && block_count == other_a.block_count && epoch == other_a.epoch;
}

bool rai::account_info::operator!= (rai::account_info const & other_a) const
{
	return !(*this == other_a);
}

size_t rai::account_info::db_size () const
{
	assert (reinterpret_cast<const uint8_t *> (this) == reinterpret_cast<const uint8_t *> (&head));
	assert (reinterpret_cast<const uint8_t *> (&head) + sizeof (head) == reinterpret_cast<const uint8_t *> (&rep_block));
	assert (reinterpret_cast<const uint8_t *> (&rep_block) + sizeof (rep_block) == reinterpret_cast<const uint8_t *> (&open_block));
	assert (reinterpret_cast<const uint8_t *> (&open_block) + sizeof (open_block) == reinterpret_cast<const uint8_t *> (&balance));
	assert (reinterpret_cast<const uint8_t *> (&balance) + sizeof (balance) == reinterpret_cast<const uint8_t *> (&modified));
	assert (reinterpret_cast<const uint8_t *> (&modified) + sizeof (modified) == reinterpret_cast<const uint8_t *> (&block_count));
	return sizeof (head) + sizeof (rep_block) + sizeof (open_block) + sizeof (balance) + sizeof (modified) + sizeof (block_count);
}

rai::block_counts::block_counts () :
send (0),
receive (0),
open (0),
change (0),
state_v0 (0),
state_v1 (0)
{
}

size_t rai::block_counts::sum ()
{
	return send + receive + open + change + state_v0 + state_v1;
}

rai::pending_info::pending_info () :
source (0),
amount (0),
epoch (rai::epoch::epoch_0)
{
}

rai::pending_info::pending_info (rai::account const & source_a, rai::amount const & amount_a, rai::epoch epoch_a) :
source (source_a),
amount (amount_a),
epoch (epoch_a)
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
	return source == other_a.source && amount == other_a.amount && epoch == other_a.epoch;
}

rai::pending_key::pending_key () :
account (0),
hash (0)
{
}

rai::pending_key::pending_key (rai::account const & account_a, rai::block_hash const & hash_a) :
account (account_a),
hash (hash_a)
{
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

rai::block_info::block_info () :
account (0),
balance (0)
{
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

bool rai::vote::operator== (rai::vote const & other_a) const
{
	auto blocks_equal (true);
	if (blocks.size () != other_a.blocks.size ())
	{
		blocks_equal = false;
	}
	else
	{
		for (auto i (0); blocks_equal && i < blocks.size (); ++i)
		{
			auto block (blocks[i]);
			auto other_block (other_a.blocks[i]);
			if (block.which () != other_block.which ())
			{
				blocks_equal = false;
			}
			else if (block.which ())
			{
				if (boost::get<rai::block_hash> (block) != boost::get<rai::block_hash> (other_block))
				{
					blocks_equal = false;
				}
			}
			else
			{
				if (!(*boost::get<std::shared_ptr<rai::block>> (block) == *boost::get<std::shared_ptr<rai::block>> (other_block)))
				{
					blocks_equal = false;
				}
			}
		}
	}
	return sequence == other_a.sequence && blocks_equal && account == other_a.account && signature == other_a.signature;
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
	boost::property_tree::ptree blocks_tree;
	for (auto block : blocks)
	{
		if (block.which ())
		{
			blocks_tree.put ("", boost::get<std::shared_ptr<rai::block>> (block)->to_json ());
		}
		else
		{
			blocks_tree.put ("", boost::get<std::shared_ptr<rai::block>> (block)->hash ().to_string ());
		}
	}
	tree.add_child ("blocks", blocks_tree);
	boost::property_tree::write_json (stream, tree);
	return stream.str ();
}

rai::amount_visitor::amount_visitor (MDB_txn * transaction_a, rai::block_store & store_a) :
transaction (transaction_a),
store (store_a),
current_amount (0),
current_balance (0),
amount (0)
{
}

void rai::amount_visitor::send_block (rai::send_block const & block_a)
{
	current_balance = block_a.hashables.previous;
	amount = block_a.hashables.balance.number ();
	current_amount = 0;
}

void rai::amount_visitor::receive_block (rai::receive_block const & block_a)
{
	current_amount = block_a.hashables.source;
}

void rai::amount_visitor::open_block (rai::open_block const & block_a)
{
	if (block_a.hashables.source != rai::genesis_account)
	{
		current_amount = block_a.hashables.source;
	}
	else
	{
		amount = rai::genesis_amount;
		current_amount = 0;
	}
}

void rai::amount_visitor::state_block (rai::state_block const & block_a)
{
	current_balance = block_a.hashables.previous;
	amount = block_a.hashables.balance.number ();
	current_amount = 0;
}

void rai::amount_visitor::change_block (rai::change_block const & block_a)
{
	amount = 0;
	current_amount = 0;
}

void rai::amount_visitor::compute (rai::block_hash const & block_hash)
{
	current_amount = block_hash;
	while (!current_amount.is_zero () || !current_balance.is_zero ())
	{
		if (!current_amount.is_zero ())
		{
			auto block (store.block_get (transaction, current_amount));
			if (block != nullptr)
			{
				block->visit (*this);
			}
			else
			{
				if (block_hash == rai::genesis_account)
				{
					amount = std::numeric_limits<rai::uint128_t>::max ();
					current_amount = 0;
				}
				else
				{
					assert (false);
					amount = 0;
					current_amount = 0;
				}
			}
		}
		else
		{
			balance_visitor prev (transaction, store);
			prev.compute (current_balance);
			amount = amount < prev.balance ? prev.balance - amount : amount - prev.balance;
			current_balance = 0;
		}
	}
}

rai::balance_visitor::balance_visitor (MDB_txn * transaction_a, rai::block_store & store_a) :
transaction (transaction_a),
store (store_a),
current_balance (0),
current_amount (0),
balance (0)
{
}

void rai::balance_visitor::send_block (rai::send_block const & block_a)
{
	balance += block_a.hashables.balance.number ();
	current_balance = 0;
}

void rai::balance_visitor::receive_block (rai::receive_block const & block_a)
{
	rai::block_info block_info;
	if (!store.block_info_get (transaction, block_a.hash (), block_info))
	{
		balance += block_info.balance.number ();
		current_balance = 0;
	}
	else
	{
		current_amount = block_a.hashables.source;
		current_balance = block_a.hashables.previous;
	}
}

void rai::balance_visitor::open_block (rai::open_block const & block_a)
{
	current_amount = block_a.hashables.source;
	current_balance = 0;
}

void rai::balance_visitor::change_block (rai::change_block const & block_a)
{
	rai::block_info block_info;
	if (!store.block_info_get (transaction, block_a.hash (), block_info))
	{
		balance += block_info.balance.number ();
		current_balance = 0;
	}
	else
	{
		current_balance = block_a.hashables.previous;
	}
}

void rai::balance_visitor::state_block (rai::state_block const & block_a)
{
	balance = block_a.hashables.balance.number ();
	current_balance = 0;
}

void rai::balance_visitor::compute (rai::block_hash const & block_hash)
{
	current_balance = block_hash;
	while (!current_balance.is_zero () || !current_amount.is_zero ())
	{
		if (!current_amount.is_zero ())
		{
			amount_visitor source (transaction, store);
			source.compute (current_amount);
			balance += source.amount;
			current_amount = 0;
		}
		else
		{
			auto block (store.block_get (transaction, current_balance));
			assert (block != nullptr);
			block->visit (*this);
		}
	}
}

rai::representative_visitor::representative_visitor (MDB_txn * transaction_a, rai::block_store & store_a) :
transaction (transaction_a),
store (store_a),
result (0)
{
}

void rai::representative_visitor::compute (rai::block_hash const & hash_a)
{
	current = hash_a;
	while (result.is_zero ())
	{
		auto block (store.block_get (transaction, current));
		assert (block != nullptr);
		block->visit (*this);
	}
}

void rai::representative_visitor::send_block (rai::send_block const & block_a)
{
	current = block_a.previous ();
}

void rai::representative_visitor::receive_block (rai::receive_block const & block_a)
{
	current = block_a.previous ();
}

void rai::representative_visitor::open_block (rai::open_block const & block_a)
{
	result = block_a.hash ();
}

void rai::representative_visitor::change_block (rai::change_block const & block_a)
{
	result = block_a.hash ();
}

void rai::representative_visitor::state_block (rai::state_block const & block_a)
{
	result = block_a.hash ();
}

rai::vote::vote (rai::vote const & other_a) :
sequence (other_a.sequence),
blocks (other_a.blocks),
account (other_a.account),
signature (other_a.signature)
{
}

rai::vote::vote (bool & error_a, rai::stream & stream_a)
{
	error_a = deserialize (stream_a);
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
					while (!error_a && stream_a.in_avail () > 0)
					{
						if (type_a == rai::block_type::not_a_block)
						{
							rai::block_hash block_hash;
							error_a = rai::read (stream_a, block_hash);
							if (!error_a)
							{
								blocks.push_back (block_hash);
							}
						}
						else
						{
							std::shared_ptr<rai::block> block (rai::deserialize_block (stream_a, type_a));
							error_a = block == nullptr;
							if (!error_a)
							{
								blocks.push_back (block);
							}
						}
					}
					if (blocks.empty ())
					{
						error_a = true;
					}
				}
			}
		}
	}
}

rai::vote::vote (rai::account const & account_a, rai::raw_key const & prv_a, uint64_t sequence_a, std::shared_ptr<rai::block> block_a) :
sequence (sequence_a),
blocks (1, block_a),
account (account_a),
signature (rai::sign_message (prv_a, account_a, hash ()))
{
}

rai::vote::vote (rai::account const & account_a, rai::raw_key const & prv_a, uint64_t sequence_a, std::vector<rai::block_hash> blocks_a) :
sequence (sequence_a),
account (account_a)
{
	assert (blocks_a.size () > 0);
	for (auto hash : blocks_a)
	{
		blocks.push_back (hash);
	}
	signature = rai::sign_message (prv_a, account_a, hash ());
}

std::string rai::vote::hashes_string () const
{
	std::string result;
	for (auto hash : *this)
	{
		result += hash.to_string ();
		result += ", ";
	}
	return result;
}

const std::string rai::vote::hash_prefix = "vote ";

rai::uint256_union rai::vote::hash () const
{
	rai::uint256_union result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result.bytes));
	if (blocks.size () > 1 || (blocks.size () > 0 && blocks[0].which ()))
	{
		blake2b_update (&hash, hash_prefix.data (), hash_prefix.size ());
	}
	for (auto block_hash : *this)
	{
		blake2b_update (&hash, block_hash.bytes.data (), sizeof (block_hash.bytes));
	}
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

void rai::vote::serialize (rai::stream & stream_a, rai::block_type type)
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, sequence);
	for (auto block : blocks)
	{
		if (block.which ())
		{
			assert (type == rai::block_type::not_a_block);
			write (stream_a, boost::get<rai::block_hash> (block));
		}
		else
		{
			if (type == rai::block_type::not_a_block)
			{
				write (stream_a, boost::get<std::shared_ptr<rai::block>> (block)->hash ());
			}
			else
			{
				boost::get<std::shared_ptr<rai::block>> (block)->serialize (stream_a);
			}
		}
	}
}

void rai::vote::serialize (rai::stream & stream_a)
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, sequence);
	for (auto block : blocks)
	{
		if (block.which ())
		{
			write (stream_a, rai::block_type::not_a_block);
			write (stream_a, boost::get<rai::block_hash> (block));
		}
		else
		{
			rai::serialize_block (stream_a, *boost::get<std::shared_ptr<rai::block>> (block));
		}
	}
}

bool rai::vote::deserialize (rai::stream & stream_a)
{
	auto result (read (stream_a, account));
	if (!result)
	{
		result = read (stream_a, signature);
		if (!result)
		{
			result = read (stream_a, sequence);
			if (!result)
			{
				rai::block_type type;
				while (!result)
				{
					if (rai::read (stream_a, type))
					{
						if (blocks.empty ())
						{
							result = true;
						}
						break;
					}
					if (!result)
					{
						if (type == rai::block_type::not_a_block)
						{
							rai::block_hash block_hash;
							result = rai::read (stream_a, block_hash);
							if (!result)
							{
								blocks.push_back (block_hash);
							}
						}
						else
						{
							std::shared_ptr<rai::block> block (rai::deserialize_block (stream_a, type));
							result = block == nullptr;
							if (!result)
							{
								blocks.push_back (block);
							}
						}
					}
				}
			}
		}
	}
	return result;
}

bool rai::vote::validate ()
{
	auto result (rai::validate_message (account, hash (), signature));
	return result;
}

rai::block_hash rai::iterate_vote_blocks_as_hash::operator() (boost::variant<std::shared_ptr<rai::block>, rai::block_hash> const & item) const
{
	rai::block_hash result;
	if (item.which ())
	{
		result = boost::get<rai::block_hash> (item);
	}
	else
	{
		result = boost::get<std::shared_ptr<rai::block>> (item)->hash ();
	}
	return result;
}

boost::transform_iterator<rai::iterate_vote_blocks_as_hash, rai::vote_blocks_vec_iter> rai::vote::begin () const
{
	return boost::transform_iterator<rai::iterate_vote_blocks_as_hash, rai::vote_blocks_vec_iter> (blocks.begin (), rai::iterate_vote_blocks_as_hash ());
}

boost::transform_iterator<rai::iterate_vote_blocks_as_hash, rai::vote_blocks_vec_iter> rai::vote::end () const
{
	return boost::transform_iterator<rai::iterate_vote_blocks_as_hash, rai::vote_blocks_vec_iter> (blocks.end (), rai::iterate_vote_blocks_as_hash ());
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

rai::block_hash rai::genesis::hash () const
{
	return open->hash ();
}
