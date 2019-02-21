#include <nano/secure/common.hpp>

#include <nano/lib/interface.h>
#include <nano/lib/numbers.hpp>
#include <nano/node/common.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <queue>

#include <crypto/ed25519-donna/ed25519.h>

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
	nano_test_account (test_public_key_data),
	nano_beta_account (beta_public_key_data),
	nano_live_account (live_public_key_data),
	nano_test_genesis (test_genesis_data),
	nano_beta_genesis (beta_genesis_data),
	nano_live_genesis (live_genesis_data),
	genesis_account (nano::is_test_network ? nano_test_account : nano::is_beta_network ? nano_beta_account : nano_live_account),
	genesis_block (nano::is_test_network ? nano_test_genesis : nano::is_beta_network ? nano_beta_genesis : nano_live_genesis),
	genesis_amount (std::numeric_limits<nano::uint128_t>::max ()),
	burn_account (0)
	{
	}
	nano::keypair zero_key;
	nano::keypair test_genesis_key;
	nano::account nano_test_account;
	nano::account nano_beta_account;
	nano::account nano_live_account;
	std::string nano_test_genesis;
	std::string nano_beta_genesis;
	std::string nano_live_genesis;
	nano::account genesis_account;
	std::string genesis_block;
	nano::uint128_t genesis_amount;
	nano::account burn_account;

	nano::account const & not_an_account ()
	{
		std::lock_guard<std::mutex> lk (mutex);
		if (!is_initialized)
		{
			// Randomly generating this means that no two nodes will ever have the same sentinel value which protects against some insecure algorithms
			nano::random_pool::generate_block (not_an_account_m.bytes.data (), not_an_account_m.bytes.size ());
			is_initialized = true;
		}
		return not_an_account_m;
	}

private:
	nano::account not_an_account_m;
	std::mutex mutex;
	bool is_initialized{ false };
};
ledger_constants globals;
}

size_t constexpr nano::send_block::size;
size_t constexpr nano::receive_block::size;
size_t constexpr nano::open_block::size;
size_t constexpr nano::change_block::size;
size_t constexpr nano::state_block::size;

nano::keypair const & nano::zero_key (globals.zero_key);
nano::keypair const & nano::test_genesis_key (globals.test_genesis_key);
nano::account const & nano::nano_test_account (globals.nano_test_account);
nano::account const & nano::nano_beta_account (globals.nano_beta_account);
nano::account const & nano::nano_live_account (globals.nano_live_account);
std::string const & nano::nano_test_genesis (globals.nano_test_genesis);
std::string const & nano::nano_beta_genesis (globals.nano_beta_genesis);
std::string const & nano::nano_live_genesis (globals.nano_live_genesis);

nano::account const & nano::genesis_account (globals.genesis_account);
std::string const & nano::genesis_block (globals.genesis_block);
nano::uint128_t const & nano::genesis_amount (globals.genesis_amount);
nano::account const & nano::burn_account (globals.burn_account);
nano::account const & nano::not_an_account ()
{
	return globals.not_an_account ();
}
// Create a new random keypair
nano::keypair::keypair ()
{
	random_pool::generate_block (prv.data.bytes.data (), prv.data.bytes.size ());
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a private key
nano::keypair::keypair (nano::raw_key && prv_a) :
prv (std::move (prv_a))
{
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a hex string of the private key
nano::keypair::keypair (std::string const & prv_a)
{
	auto error (prv.data.decode_hex (prv_a));
	assert (!error);
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Serialize a block prefixed with an 8-bit typecode
void nano::serialize_block (nano::stream & stream_a, nano::block const & block_a)
{
	write (stream_a, block_a.type ());
	block_a.serialize (stream_a);
}

nano::account_info::account_info () :
head (0),
rep_block (0),
open_block (0),
balance (0),
modified (0),
block_count (0),
epoch (nano::epoch::epoch_0)
{
}

nano::account_info::account_info (nano::block_hash const & head_a, nano::block_hash const & rep_block_a, nano::block_hash const & open_block_a, nano::amount const & balance_a, uint64_t modified_a, uint64_t block_count_a, nano::epoch epoch_a) :
head (head_a),
rep_block (rep_block_a),
open_block (open_block_a),
balance (balance_a),
modified (modified_a),
block_count (block_count_a),
epoch (epoch_a)
{
}

void nano::account_info::serialize (nano::stream & stream_a) const
{
	write (stream_a, head.bytes);
	write (stream_a, rep_block.bytes);
	write (stream_a, open_block.bytes);
	write (stream_a, balance.bytes);
	write (stream_a, modified);
	write (stream_a, block_count);
}

bool nano::account_info::deserialize (nano::stream & stream_a)
{
	auto error (false);
	try
	{
		nano::read (stream_a, head.bytes);
		nano::read (stream_a, rep_block.bytes);
		nano::read (stream_a, open_block.bytes);
		nano::read (stream_a, balance.bytes);
		nano::read (stream_a, modified);
		nano::read (stream_a, block_count);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool nano::account_info::operator== (nano::account_info const & other_a) const
{
	return head == other_a.head && rep_block == other_a.rep_block && open_block == other_a.open_block && balance == other_a.balance && modified == other_a.modified && block_count == other_a.block_count && epoch == other_a.epoch;
}

bool nano::account_info::operator!= (nano::account_info const & other_a) const
{
	return !(*this == other_a);
}

size_t nano::account_info::db_size () const
{
	assert (reinterpret_cast<const uint8_t *> (this) == reinterpret_cast<const uint8_t *> (&head));
	assert (reinterpret_cast<const uint8_t *> (&head) + sizeof (head) == reinterpret_cast<const uint8_t *> (&rep_block));
	assert (reinterpret_cast<const uint8_t *> (&rep_block) + sizeof (rep_block) == reinterpret_cast<const uint8_t *> (&open_block));
	assert (reinterpret_cast<const uint8_t *> (&open_block) + sizeof (open_block) == reinterpret_cast<const uint8_t *> (&balance));
	assert (reinterpret_cast<const uint8_t *> (&balance) + sizeof (balance) == reinterpret_cast<const uint8_t *> (&modified));
	assert (reinterpret_cast<const uint8_t *> (&modified) + sizeof (modified) == reinterpret_cast<const uint8_t *> (&block_count));
	return sizeof (head) + sizeof (rep_block) + sizeof (open_block) + sizeof (balance) + sizeof (modified) + sizeof (block_count);
}

nano::block_counts::block_counts () :
send (0),
receive (0),
open (0),
change (0),
state_v0 (0),
state_v1 (0)
{
}

size_t nano::block_counts::sum ()
{
	return send + receive + open + change + state_v0 + state_v1;
}

nano::pending_info::pending_info () :
source (0),
amount (0),
epoch (nano::epoch::epoch_0)
{
}

nano::pending_info::pending_info (nano::account const & source_a, nano::amount const & amount_a, nano::epoch epoch_a) :
source (source_a),
amount (amount_a),
epoch (epoch_a)
{
}

void nano::pending_info::serialize (nano::stream & stream_a) const
{
	nano::write (stream_a, source.bytes);
	nano::write (stream_a, amount.bytes);
}

bool nano::pending_info::deserialize (nano::stream & stream_a)
{
	auto error (false);
	try
	{
		nano::read (stream_a, source.bytes);
		nano::read (stream_a, amount.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool nano::pending_info::operator== (nano::pending_info const & other_a) const
{
	return source == other_a.source && amount == other_a.amount && epoch == other_a.epoch;
}

nano::pending_key::pending_key () :
account (0),
hash (0)
{
}

nano::pending_key::pending_key (nano::account const & account_a, nano::block_hash const & hash_a) :
account (account_a),
hash (hash_a)
{
}

void nano::pending_key::serialize (nano::stream & stream_a) const
{
	nano::write (stream_a, account.bytes);
	nano::write (stream_a, hash.bytes);
}

bool nano::pending_key::deserialize (nano::stream & stream_a)
{
	auto error (false);
	try
	{
		nano::read (stream_a, account.bytes);
		nano::read (stream_a, hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool nano::pending_key::operator== (nano::pending_key const & other_a) const
{
	return account == other_a.account && hash == other_a.hash;
}

nano::block_hash nano::pending_key::key () const
{
	return account;
}

nano::unchecked_info::unchecked_info () :
block (nullptr),
account (0),
modified (0),
verified (nano::signature_verification::unknown)
{
}

nano::unchecked_info::unchecked_info (std::shared_ptr<nano::block> block_a, nano::account const & account_a, uint64_t modified_a, nano::signature_verification verified_a) :
block (block_a),
account (account_a),
modified (modified_a),
verified (verified_a)
{
}

void nano::unchecked_info::serialize (nano::stream & stream_a) const
{
	assert (block != nullptr);
	nano::serialize_block (stream_a, *block);
	nano::write (stream_a, account.bytes);
	nano::write (stream_a, modified);
	nano::write (stream_a, verified);
}

bool nano::unchecked_info::deserialize (nano::stream & stream_a)
{
	block = nano::deserialize_block (stream_a);
	bool error (block == nullptr);
	if (!error)
	{
		try
		{
			nano::read (stream_a, account.bytes);
			nano::read (stream_a, modified);
			nano::read (stream_a, verified);
		}
		catch (std::runtime_error const &)
		{
			error = true;
		}
	}
	return error;
}

bool nano::unchecked_info::operator== (nano::unchecked_info const & other_a) const
{
	return block->hash () == other_a.block->hash () && account == other_a.account && modified == other_a.modified && verified == other_a.verified;
}

nano::endpoint_key::endpoint_key (const std::array<uint8_t, 16> & address_a, uint16_t port_a) :
address (address_a), network_port (boost::endian::native_to_big (port_a))
{
}

const std::array<uint8_t, 16> & nano::endpoint_key::address_bytes () const
{
	return address;
}

uint16_t nano::endpoint_key::port () const
{
	return boost::endian::big_to_native (network_port);
}

nano::block_info::block_info () :
account (0),
balance (0)
{
}

nano::block_info::block_info (nano::account const & account_a, nano::amount const & balance_a) :
account (account_a),
balance (balance_a)
{
}

void nano::block_info::serialize (nano::stream & stream_a) const
{
	nano::write (stream_a, account.bytes);
	nano::write (stream_a, balance.bytes);
}

bool nano::block_info::deserialize (nano::stream & stream_a)
{
	auto error (false);
	try
	{
		nano::read (stream_a, account.bytes);
		nano::read (stream_a, balance.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool nano::block_info::operator== (nano::block_info const & other_a) const
{
	return account == other_a.account && balance == other_a.balance;
}

bool nano::vote::operator== (nano::vote const & other_a) const
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
				if (boost::get<nano::block_hash> (block) != boost::get<nano::block_hash> (other_block))
				{
					blocks_equal = false;
				}
			}
			else
			{
				if (!(*boost::get<std::shared_ptr<nano::block>> (block) == *boost::get<std::shared_ptr<nano::block>> (other_block)))
				{
					blocks_equal = false;
				}
			}
		}
	}
	return sequence == other_a.sequence && blocks_equal && account == other_a.account && signature == other_a.signature;
}

bool nano::vote::operator!= (nano::vote const & other_a) const
{
	return !(*this == other_a);
}

std::string nano::vote::to_json () const
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
			blocks_tree.put ("", boost::get<std::shared_ptr<nano::block>> (block)->to_json ());
		}
		else
		{
			blocks_tree.put ("", boost::get<std::shared_ptr<nano::block>> (block)->hash ().to_string ());
		}
	}
	tree.add_child ("blocks", blocks_tree);
	boost::property_tree::write_json (stream, tree);
	return stream.str ();
}

nano::vote::vote (nano::vote const & other_a) :
sequence (other_a.sequence),
blocks (other_a.blocks),
account (other_a.account),
signature (other_a.signature)
{
}

nano::vote::vote (bool & error_a, nano::stream & stream_a, nano::block_uniquer * uniquer_a)
{
	error_a = deserialize (stream_a, uniquer_a);
}

nano::vote::vote (bool & error_a, nano::stream & stream_a, nano::block_type type_a, nano::block_uniquer * uniquer_a)
{
	try
	{
		nano::read (stream_a, account.bytes);
		nano::read (stream_a, signature.bytes);
		nano::read (stream_a, sequence);

		while (stream_a.in_avail () > 0)
		{
			if (type_a == nano::block_type::not_a_block)
			{
				nano::block_hash block_hash;
				nano::read (stream_a, block_hash);
				blocks.push_back (block_hash);
			}
			else
			{
				std::shared_ptr<nano::block> block (nano::deserialize_block (stream_a, type_a, uniquer_a));
				if (block == nullptr)
				{
					throw std::runtime_error ("Block is null");
				}
				blocks.push_back (block);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}

	if (blocks.empty ())
	{
		error_a = true;
	}
}

nano::vote::vote (nano::account const & account_a, nano::raw_key const & prv_a, uint64_t sequence_a, std::shared_ptr<nano::block> block_a) :
sequence (sequence_a),
blocks (1, block_a),
account (account_a),
signature (nano::sign_message (prv_a, account_a, hash ()))
{
}

nano::vote::vote (nano::account const & account_a, nano::raw_key const & prv_a, uint64_t sequence_a, std::vector<nano::block_hash> blocks_a) :
sequence (sequence_a),
account (account_a)
{
	assert (blocks_a.size () > 0);
	assert (blocks_a.size () <= 12);
	for (auto hash : blocks_a)
	{
		blocks.push_back (hash);
	}
	signature = nano::sign_message (prv_a, account_a, hash ());
}

std::string nano::vote::hashes_string () const
{
	std::string result;
	for (auto hash : *this)
	{
		result += hash.to_string ();
		result += ", ";
	}
	return result;
}

const std::string nano::vote::hash_prefix = "vote ";

nano::uint256_union nano::vote::hash () const
{
	nano::uint256_union result;
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

nano::uint256_union nano::vote::full_hash () const
{
	nano::uint256_union result;
	blake2b_state state;
	blake2b_init (&state, sizeof (result.bytes));
	blake2b_update (&state, hash ().bytes.data (), sizeof (hash ().bytes));
	blake2b_update (&state, account.bytes.data (), sizeof (account.bytes.data ()));
	blake2b_update (&state, signature.bytes.data (), sizeof (signature.bytes.data ()));
	blake2b_final (&state, result.bytes.data (), sizeof (result.bytes));
	return result;
}

void nano::vote::serialize (nano::stream & stream_a, nano::block_type type)
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, sequence);
	for (auto block : blocks)
	{
		if (block.which ())
		{
			assert (type == nano::block_type::not_a_block);
			write (stream_a, boost::get<nano::block_hash> (block));
		}
		else
		{
			if (type == nano::block_type::not_a_block)
			{
				write (stream_a, boost::get<std::shared_ptr<nano::block>> (block)->hash ());
			}
			else
			{
				boost::get<std::shared_ptr<nano::block>> (block)->serialize (stream_a);
			}
		}
	}
}

void nano::vote::serialize (nano::stream & stream_a)
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, sequence);
	for (auto block : blocks)
	{
		if (block.which ())
		{
			write (stream_a, nano::block_type::not_a_block);
			write (stream_a, boost::get<nano::block_hash> (block));
		}
		else
		{
			nano::serialize_block (stream_a, *boost::get<std::shared_ptr<nano::block>> (block));
		}
	}
}

bool nano::vote::deserialize (nano::stream & stream_a, nano::block_uniquer * uniquer_a)
{
	auto error (false);
	try
	{
		nano::read (stream_a, account);
		nano::read (stream_a, signature);
		nano::read (stream_a, sequence);

		nano::block_type type;

		while (true)
		{
			if (nano::try_read (stream_a, type))
			{
				// Reached the end of the stream
				break;
			}

			if (type == nano::block_type::not_a_block)
			{
				nano::block_hash block_hash;
				nano::read (stream_a, block_hash);
				blocks.push_back (block_hash);
			}
			else
			{
				std::shared_ptr<nano::block> block (nano::deserialize_block (stream_a, type, uniquer_a));
				if (block == nullptr)
				{
					throw std::runtime_error ("Block is empty");
				}

				blocks.push_back (block);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	if (blocks.empty ())
	{
		error = true;
	}

	return error;
}

bool nano::vote::validate ()
{
	auto result (nano::validate_message (account, hash (), signature));
	return result;
}

nano::block_hash nano::iterate_vote_blocks_as_hash::operator() (boost::variant<std::shared_ptr<nano::block>, nano::block_hash> const & item) const
{
	nano::block_hash result;
	if (item.which ())
	{
		result = boost::get<nano::block_hash> (item);
	}
	else
	{
		result = boost::get<std::shared_ptr<nano::block>> (item)->hash ();
	}
	return result;
}

boost::transform_iterator<nano::iterate_vote_blocks_as_hash, nano::vote_blocks_vec_iter> nano::vote::begin () const
{
	return boost::transform_iterator<nano::iterate_vote_blocks_as_hash, nano::vote_blocks_vec_iter> (blocks.begin (), nano::iterate_vote_blocks_as_hash ());
}

boost::transform_iterator<nano::iterate_vote_blocks_as_hash, nano::vote_blocks_vec_iter> nano::vote::end () const
{
	return boost::transform_iterator<nano::iterate_vote_blocks_as_hash, nano::vote_blocks_vec_iter> (blocks.end (), nano::iterate_vote_blocks_as_hash ());
}

nano::vote_uniquer::vote_uniquer (nano::block_uniquer & uniquer_a) :
uniquer (uniquer_a)
{
}

std::shared_ptr<nano::vote> nano::vote_uniquer::unique (std::shared_ptr<nano::vote> vote_a)
{
	auto result (vote_a);
	if (result != nullptr && !result->blocks.empty ())
	{
		if (!result->blocks[0].which ())
		{
			result->blocks[0] = uniquer.unique (boost::get<std::shared_ptr<nano::block>> (result->blocks[0]));
		}
		nano::uint256_union key (vote_a->full_hash ());
		std::lock_guard<std::mutex> lock (mutex);
		auto & existing (votes[key]);
		if (auto block_l = existing.lock ())
		{
			result = block_l;
		}
		else
		{
			existing = vote_a;
		}

		release_assert (std::numeric_limits<CryptoPP::word32>::max () > votes.size ());
		for (auto i (0); i < cleanup_count && votes.size () > 0; ++i)
		{
			auto random_offset = nano::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (votes.size () - 1));

			auto existing (std::next (votes.begin (), random_offset));
			if (existing == votes.end ())
			{
				existing = votes.begin ();
			}
			if (existing != votes.end ())
			{
				if (auto block_l = existing->second.lock ())
				{
					// Still live
				}
				else
				{
					votes.erase (existing);
				}
			}
		}
	}
	return result;
}

size_t nano::vote_uniquer::size ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return votes.size ();
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_uniquer & vote_uniquer, const std::string & name)
{
	auto count = vote_uniquer.size ();
	auto sizeof_element = sizeof (vote_uniquer::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "votes", count, sizeof_element }));
	return composite;
}
}

nano::genesis::genesis ()
{
	boost::property_tree::ptree tree;
	std::stringstream istream (nano::genesis_block);
	boost::property_tree::read_json (istream, tree);
	open = nano::deserialize_block_json (tree);
	assert (open != nullptr);
}

nano::block_hash nano::genesis::hash () const
{
	return open->hash ();
}
