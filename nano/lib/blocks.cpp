#include <celerix/crypto_lib/random_pool.hpp>
#include <celerix/lib/block_type.hpp>
#include <celerix/lib/block_uniquer.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/enum_util.hpp>
#include <celerix/lib/memory.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/stream.hpp>
#include <celerix/lib/threading.hpp>
#include <celerix/lib/work_version.hpp>
#include <celerix/secure/common.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <bitset>

#include <cryptopp/words.h>

size_t constexpr celerix::send_block::size;
size_t constexpr celerix::receive_block::size;
size_t constexpr celerix::open_block::size;
size_t constexpr celerix::change_block::size;
size_t constexpr celerix::state_block::size;

/** Compare blocks, first by type, then content. This is an optimization over dynamic_cast, which is very slow on some platforms. */
namespace
{
template <typename T>
bool blocks_equal (T const & first, celerix::block const & second)
{
	static_assert (std::is_base_of<celerix::block, T>::value, "Input parameter is not a block type");
	return (first.type () == second.type ()) && (static_cast<T const &> (second)) == first;
}

template <typename block>
std::shared_ptr<block> deserialize_block (celerix::stream & stream_a)
{
	auto error (false);
	auto result = celerix::make_shared<block> (error, stream_a);
	if (error)
	{
		result = nullptr;
	}

	return result;
}
}

void celerix::block_memory_pool_purge ()
{
	celerix::purge_shared_ptr_singleton_pool_memory<celerix::open_block> ();
	celerix::purge_shared_ptr_singleton_pool_memory<celerix::state_block> ();
	celerix::purge_shared_ptr_singleton_pool_memory<celerix::send_block> ();
	celerix::purge_shared_ptr_singleton_pool_memory<celerix::change_block> ();
}

/*
 * block
 */

std::string celerix::block::to_json () const
{
	std::string result;
	serialize_json (result);
	return result;
}

size_t celerix::block::size (celerix::block_type type_a)
{
	size_t result (0);
	switch (type_a)
	{
		case celerix::block_type::invalid:
		case celerix::block_type::not_a_block:
			debug_assert (false);
			break;
		case celerix::block_type::send:
			result = celerix::send_block::size;
			break;
		case celerix::block_type::receive:
			result = celerix::receive_block::size;
			break;
		case celerix::block_type::change:
			result = celerix::change_block::size;
			break;
		case celerix::block_type::open:
			result = celerix::open_block::size;
			break;
		case celerix::block_type::state:
			result = celerix::state_block::size;
			break;
	}
	return result;
}

celerix::work_version celerix::block::work_version () const
{
	return celerix::work_version::work_1;
}

celerix::block_hash celerix::block::generate_hash () const
{
	celerix::block_hash result;
	blake2b_state hash_l;
	auto status (blake2b_init (&hash_l, sizeof (result.bytes)));
	debug_assert (status == 0);
	generate_hash (hash_l);
	status = blake2b_final (&hash_l, result.bytes.data (), sizeof (result.bytes));
	debug_assert (status == 0);
	return result;
}

void celerix::block::refresh ()
{
	if (!cached_hash.is_zero ())
	{
		cached_hash = generate_hash ();
	}
}

bool celerix::block::is_send () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case celerix::block_type::send:
			return true;
		case celerix::block_type::state:
			return sideband ().details.is_send;
		default:
			return false;
	}
}

bool celerix::block::is_receive () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case celerix::block_type::receive:
		case celerix::block_type::open:
			return true;
		case celerix::block_type::state:
			return sideband ().details.is_receive;
		default:
			return false;
	}
}

bool celerix::block::is_change () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case celerix::block_type::change:
			return true;
		case celerix::block_type::state:
			if (link_field ().value ().is_zero ())
			{
				return true;
			}
			return false;
		default:
			return false;
	}
}

bool celerix::block::is_epoch () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case celerix::block_type::state:
			return sideband ().details.is_epoch;
		default:
			return false;
	}
}

celerix::block_hash const & celerix::block::hash () const
{
	if (!cached_hash.is_zero ())
	{
		// Once a block is created, it should not be modified (unless using refresh ())
		// This would invalidate the cache; check it hasn't changed.
		debug_assert (cached_hash == generate_hash ());
	}
	else
	{
		cached_hash = generate_hash ();
	}

	return cached_hash;
}

celerix::block_hash celerix::block::full_hash () const
{
	celerix::block_hash result;
	blake2b_state state;
	blake2b_init (&state, sizeof (result.bytes));
	blake2b_update (&state, hash ().bytes.data (), sizeof (hash ()));
	auto signature (block_signature ());
	blake2b_update (&state, signature.bytes.data (), sizeof (signature));
	auto work (block_work ());
	blake2b_update (&state, &work, sizeof (work));
	blake2b_final (&state, result.bytes.data (), sizeof (result.bytes));
	return result;
}

celerix::block_sideband const & celerix::block::sideband () const
{
	release_assert (sideband_m.is_initialized ());
	return *sideband_m;
}

void celerix::block::sideband_set (celerix::block_sideband const & sideband_a)
{
	sideband_m = sideband_a;
}

bool celerix::block::has_sideband () const
{
	return sideband_m.is_initialized ();
}

std::optional<celerix::account> celerix::block::representative_field () const
{
	return std::nullopt;
}

std::optional<celerix::block_hash> celerix::block::source_field () const
{
	return std::nullopt;
}

std::optional<celerix::account> celerix::block::destination_field () const
{
	return std::nullopt;
}

std::optional<celerix::link> celerix::block::link_field () const
{
	return std::nullopt;
}

celerix::account celerix::block::account () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case block_type::open:
		case block_type::state:
			return account_field ().value ();
		case block_type::change:
		case block_type::send:
		case block_type::receive:
			return sideband ().account;
		default:
			release_assert (false);
	}
}

celerix::amount celerix::block::balance () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case celerix::block_type::open:
		case celerix::block_type::receive:
		case celerix::block_type::change:
			return sideband ().balance;
		case celerix::block_type::send:
		case celerix::block_type::state:
			return balance_field ().value ();
		default:
			release_assert (false);
	}
}

celerix::account celerix::block::destination () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case celerix::block_type::send:
			return destination_field ().value ();
		case celerix::block_type::state:
			release_assert (sideband ().details.is_send);
			return link_field ().value ().as_account ();
		default:
			release_assert (false);
	}
}

celerix::block_hash celerix::block::source () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case celerix::block_type::open:
		case celerix::block_type::receive:
			return source_field ().value ();
		case celerix::block_type::state:
			release_assert (sideband ().details.is_receive);
			return link_field ().value ().as_block_hash ();
		default:
			release_assert (false);
	}
}

// TODO - Remove comments below and fixup usages to not need to check .is_zero ()
// std::optional<celerix::block_hash> celerix::block::previous () const
celerix::block_hash celerix::block::previous () const noexcept
{
	std::optional<celerix::block_hash> result = previous_field ();
	/*
	if (result && result.value ().is_zero ())
	{
		return std::nullopt;
	}
	return result;*/
	return result.value_or (0);
}

std::optional<celerix::account> celerix::block::account_field () const
{
	return std::nullopt;
}

celerix::qualified_root celerix::block::qualified_root () const
{
	return { root (), previous () };
}

std::optional<celerix::amount> celerix::block::balance_field () const
{
	return std::nullopt;
}

void celerix::block::operator() (celerix::object_stream & obs) const
{
	obs.write ("type", type ());
	obs.write ("hash", hash ());

	if (has_sideband ())
	{
		obs.write ("sideband", sideband ());
	}
}

/*
 * send_block
 */

void celerix::send_block::visit (celerix::block_visitor & visitor_a) const
{
	visitor_a.send_block (*this);
}

void celerix::send_block::visit (celerix::mutable_block_visitor & visitor_a)
{
	visitor_a.send_block (*this);
}

void celerix::send_block::generate_hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t celerix::send_block::block_work () const
{
	return work;
}

void celerix::send_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

celerix::send_hashables::send_hashables (celerix::block_hash const & previous_a, celerix::account const & destination_a, celerix::amount const & balance_a) :
	previous (previous_a),
	destination (destination_a),
	balance (balance_a)
{
}

celerix::send_hashables::send_hashables (bool & error_a, celerix::stream & stream_a)
{
	try
	{
		celerix::read (stream_a, previous.bytes);
		celerix::read (stream_a, destination.bytes);
		celerix::read (stream_a, balance.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

celerix::send_hashables::send_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = destination.decode_account (destination_l);
			if (!error_a)
			{
				error_a = balance.decode_hex (balance_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void celerix::send_hashables::hash (blake2b_state & hash_a) const
{
	auto status (blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes)));
	debug_assert (status == 0);
	status = blake2b_update (&hash_a, destination.bytes.data (), sizeof (destination.bytes));
	debug_assert (status == 0);
	status = blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	debug_assert (status == 0);
}

void celerix::send_block::serialize (celerix::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.destination.bytes);
	write (stream_a, hashables.balance.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

bool celerix::send_block::deserialize (celerix::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous.bytes);
		read (stream_a, hashables.destination.bytes);
		read (stream_a, hashables.balance.bytes);
		read (stream_a, signature.bytes);
		read (stream_a, work);
	}
	catch (std::exception const &)
	{
		error = true;
	}

	return error;
}

void celerix::send_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void celerix::send_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "send");
	std::string previous;
	hashables.previous.encode_hex (previous);
	tree.put ("previous", previous);
	tree.put ("destination", hashables.destination.to_account ());
	std::string balance;
	hashables.balance.encode_hex (balance);
	tree.put ("balance", balance);
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", celerix::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool celerix::send_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "send");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.destination.decode_account (destination_l);
			if (!error)
			{
				error = hashables.balance.decode_hex (balance_l);
				if (!error)
				{
					error = celerix::from_string_hex (work_l, work);
					if (!error)
					{
						error = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

celerix::send_block::send_block (celerix::block_hash const & previous_a, celerix::account const & destination_a, celerix::amount const & balance_a, celerix::raw_key const & prv_a, celerix::public_key const & pub_a, uint64_t work_a) :
	hashables (previous_a, destination_a, balance_a),
	signature (celerix::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (destination_a != nullptr);
	debug_assert (pub_a != nullptr);
}

celerix::send_block::send_block (bool & error_a, celerix::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			celerix::read (stream_a, signature.bytes);
			celerix::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

celerix::send_block::send_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = celerix::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

std::shared_ptr<celerix::block> celerix::send_block::clone () const
{
	return std::make_shared<celerix::send_block> (*this);
}

bool celerix::send_block::operator== (celerix::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool celerix::send_block::valid_predecessor (celerix::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case celerix::block_type::send:
		case celerix::block_type::receive:
		case celerix::block_type::open:
		case celerix::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

celerix::block_type celerix::send_block::type () const
{
	return celerix::block_type::send;
}

bool celerix::send_block::operator== (celerix::send_block const & other_a) const
{
	auto result (hashables.destination == other_a.hashables.destination && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance && work == other_a.work && signature == other_a.signature);
	return result;
}

std::optional<celerix::block_hash> celerix::send_block::previous_field () const
{
	return hashables.previous;
}

std::optional<celerix::account> celerix::send_block::destination_field () const
{
	return hashables.destination;
}

celerix::root celerix::send_block::root () const
{
	return hashables.previous;
}

std::optional<celerix::amount> celerix::send_block::balance_field () const
{
	return hashables.balance;
}

celerix::signature const & celerix::send_block::block_signature () const
{
	return signature;
}

void celerix::send_block::signature_set (celerix::signature const & signature_a)
{
	signature = signature_a;
}

void celerix::send_block::operator() (celerix::object_stream & obs) const
{
	celerix::block::operator() (obs); // Write common data

	obs.write ("previous", hashables.previous);
	obs.write ("destination", hashables.destination);
	obs.write ("balance", hashables.balance);
	obs.write ("signature", signature);
	obs.write ("work", work);
}

/*
 * open_block
 */

celerix::open_hashables::open_hashables (celerix::block_hash const & source_a, celerix::account const & representative_a, celerix::account const & account_a) :
	source (source_a),
	representative (representative_a),
	account (account_a)
{
}

celerix::open_hashables::open_hashables (bool & error_a, celerix::stream & stream_a)
{
	try
	{
		celerix::read (stream_a, source.bytes);
		celerix::read (stream_a, representative.bytes);
		celerix::read (stream_a, account.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

celerix::open_hashables::open_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		error_a = source.decode_hex (source_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
			if (!error_a)
			{
				error_a = account.decode_account (account_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void celerix::open_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
}

celerix::open_block::open_block (celerix::block_hash const & source_a, celerix::account const & representative_a, celerix::account const & account_a, celerix::raw_key const & prv_a, celerix::public_key const & pub_a, uint64_t work_a) :
	hashables (source_a, representative_a, account_a),
	signature (celerix::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (representative_a != nullptr);
	debug_assert (account_a != nullptr);
	debug_assert (pub_a != nullptr);
}

celerix::open_block::open_block (celerix::block_hash const & source_a, celerix::account const & representative_a, celerix::account const & account_a, std::nullptr_t) :
	hashables (source_a, representative_a, account_a),
	work (0)
{
	debug_assert (representative_a != nullptr);
	debug_assert (account_a != nullptr);

	signature.clear ();
}

celerix::open_block::open_block (bool & error_a, celerix::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			celerix::read (stream_a, signature);
			celerix::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

celerix::open_block::open_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = celerix::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

std::shared_ptr<celerix::block> celerix::open_block::clone () const
{
	return std::make_shared<celerix::open_block> (*this);
}

void celerix::open_block::generate_hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t celerix::open_block::block_work () const
{
	return work;
}

void celerix::open_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

std::optional<celerix::block_hash> celerix::open_block::previous_field () const
{
	return std::nullopt;
}

std::optional<celerix::account> celerix::open_block::account_field () const
{
	return hashables.account;
}

void celerix::open_block::serialize (celerix::stream & stream_a) const
{
	write (stream_a, hashables.source);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.account);
	write (stream_a, signature);
	write (stream_a, work);
}

bool celerix::open_block::deserialize (celerix::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.source);
		read (stream_a, hashables.representative);
		read (stream_a, hashables.account);
		read (stream_a, signature);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void celerix::open_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void celerix::open_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "open");
	tree.put ("source", hashables.source.to_string ());
	tree.put ("representative", hashables.representative.to_account ());
	tree.put ("account", hashables.account.to_account ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", celerix::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool celerix::open_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "open");
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.source.decode_hex (source_l);
		if (!error)
		{
			error = hashables.representative.decode_hex (representative_l);
			if (!error)
			{
				error = hashables.account.decode_hex (account_l);
				if (!error)
				{
					error = celerix::from_string_hex (work_l, work);
					if (!error)
					{
						error = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void celerix::open_block::visit (celerix::block_visitor & visitor_a) const
{
	visitor_a.open_block (*this);
}

void celerix::open_block::visit (celerix::mutable_block_visitor & visitor_a)
{
	visitor_a.open_block (*this);
}

celerix::block_type celerix::open_block::type () const
{
	return celerix::block_type::open;
}

bool celerix::open_block::operator== (celerix::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool celerix::open_block::operator== (celerix::open_block const & other_a) const
{
	return hashables.source == other_a.hashables.source && hashables.representative == other_a.hashables.representative && hashables.account == other_a.hashables.account && work == other_a.work && signature == other_a.signature;
}

bool celerix::open_block::valid_predecessor (celerix::block const & block_a) const
{
	return false;
}

std::optional<celerix::block_hash> celerix::open_block::source_field () const
{
	return hashables.source;
}

celerix::root celerix::open_block::root () const
{
	return hashables.account;
}

std::optional<celerix::account> celerix::open_block::representative_field () const
{
	return hashables.representative;
}

celerix::signature const & celerix::open_block::block_signature () const
{
	return signature;
}

void celerix::open_block::signature_set (celerix::signature const & signature_a)
{
	signature = signature_a;
}

void celerix::open_block::operator() (celerix::object_stream & obs) const
{
	celerix::block::operator() (obs); // Write common data

	obs.write ("source", hashables.source);
	obs.write ("representative", hashables.representative);
	obs.write ("account", hashables.account);
	obs.write ("signature", signature);
	obs.write ("work", work);
}

/*
 * change_block
 */

celerix::change_hashables::change_hashables (celerix::block_hash const & previous_a, celerix::account const & representative_a) :
	previous (previous_a),
	representative (representative_a)
{
}

celerix::change_hashables::change_hashables (bool & error_a, celerix::stream & stream_a)
{
	try
	{
		celerix::read (stream_a, previous);
		celerix::read (stream_a, representative);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

celerix::change_hashables::change_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void celerix::change_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
}

celerix::change_block::change_block (celerix::block_hash const & previous_a, celerix::account const & representative_a, celerix::raw_key const & prv_a, celerix::public_key const & pub_a, uint64_t work_a) :
	hashables (previous_a, representative_a),
	signature (celerix::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (representative_a != nullptr);
	debug_assert (pub_a != nullptr);
}

celerix::change_block::change_block (bool & error_a, celerix::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			celerix::read (stream_a, signature);
			celerix::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

celerix::change_block::change_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = celerix::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

std::shared_ptr<celerix::block> celerix::change_block::clone () const
{
	return std::make_shared<celerix::change_block> (*this);
}

void celerix::change_block::generate_hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t celerix::change_block::block_work () const
{
	return work;
}

void celerix::change_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

std::optional<celerix::block_hash> celerix::change_block::previous_field () const
{
	return hashables.previous;
}

void celerix::change_block::serialize (celerix::stream & stream_a) const
{
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, signature);
	write (stream_a, work);
}

bool celerix::change_block::deserialize (celerix::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous);
		read (stream_a, hashables.representative);
		read (stream_a, signature);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void celerix::change_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void celerix::change_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "change");
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", hashables.representative.to_account ());
	tree.put ("work", celerix::to_string_hex (work));
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
}

bool celerix::change_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "change");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.representative.decode_hex (representative_l);
			if (!error)
			{
				error = celerix::from_string_hex (work_l, work);
				if (!error)
				{
					error = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void celerix::change_block::visit (celerix::block_visitor & visitor_a) const
{
	visitor_a.change_block (*this);
}

void celerix::change_block::visit (celerix::mutable_block_visitor & visitor_a)
{
	visitor_a.change_block (*this);
}

celerix::block_type celerix::change_block::type () const
{
	return celerix::block_type::change;
}

bool celerix::change_block::operator== (celerix::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool celerix::change_block::operator== (celerix::change_block const & other_a) const
{
	return hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && work == other_a.work && signature == other_a.signature;
}

bool celerix::change_block::valid_predecessor (celerix::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case celerix::block_type::send:
		case celerix::block_type::receive:
		case celerix::block_type::open:
		case celerix::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

celerix::root celerix::change_block::root () const
{
	return hashables.previous;
}

std::optional<celerix::account> celerix::change_block::representative_field () const
{
	return hashables.representative;
}

celerix::signature const & celerix::change_block::block_signature () const
{
	return signature;
}

void celerix::change_block::signature_set (celerix::signature const & signature_a)
{
	signature = signature_a;
}

void celerix::change_block::operator() (celerix::object_stream & obs) const
{
	celerix::block::operator() (obs); // Write common data

	obs.write ("previous", hashables.previous);
	obs.write ("representative", hashables.representative);
	obs.write ("signature", signature);
	obs.write ("work", work);
}

/*
 * state_block
 */

celerix::state_hashables::state_hashables (celerix::account const & account_a, celerix::block_hash const & previous_a, celerix::account const & representative_a, celerix::amount const & balance_a, celerix::link const & link_a) :
	account (account_a),
	previous (previous_a),
	representative (representative_a),
	balance (balance_a),
	link (link_a)
{
}

celerix::state_hashables::state_hashables (bool & error_a, celerix::stream & stream_a)
{
	try
	{
		celerix::read (stream_a, account);
		celerix::read (stream_a, previous);
		celerix::read (stream_a, representative);
		celerix::read (stream_a, balance);
		celerix::read (stream_a, link);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

celerix::state_hashables::state_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto link_l (tree_a.get<std::string> ("link"));
		error_a = account.decode_account (account_l);
		if (!error_a)
		{
			error_a = previous.decode_hex (previous_l);
			if (!error_a)
			{
				error_a = representative.decode_account (representative_l);
				if (!error_a)
				{
					error_a = balance.decode_dec (balance_l);
					if (!error_a)
					{
						error_a = link.decode_account (link_l) && link.decode_hex (link_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void celerix::state_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	blake2b_update (&hash_a, link.bytes.data (), sizeof (link.bytes));
}

celerix::state_block::state_block (celerix::account const & account_a, celerix::block_hash const & previous_a, celerix::account const & representative_a, celerix::amount const & balance_a, celerix::link const & link_a, celerix::raw_key const & prv_a, celerix::public_key const & pub_a, uint64_t work_a) :
	hashables (account_a, previous_a, representative_a, balance_a, link_a),
	signature (celerix::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (account_a != nullptr);
	debug_assert (representative_a != nullptr);
	debug_assert (link_a.as_account () != nullptr);
	debug_assert (pub_a != nullptr);
}

celerix::state_block::state_block (bool & error_a, celerix::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			celerix::read (stream_a, signature);
			celerix::read (stream_a, work);
			boost::endian::big_to_native_inplace (work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

celerix::state_block::state_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto type_l (tree_a.get<std::string> ("type"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = type_l != "state";
			if (!error_a)
			{
				error_a = celerix::from_string_hex (work_l, work);
				if (!error_a)
				{
					error_a = signature.decode_hex (signature_l);
				}
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

std::shared_ptr<celerix::block> celerix::state_block::clone () const
{
	return std::make_shared<celerix::state_block> (*this);
}

void celerix::state_block::generate_hash (blake2b_state & hash_a) const
{
	celerix::uint256_union preamble (static_cast<uint64_t> (celerix::block_type::state));
	blake2b_update (&hash_a, preamble.bytes.data (), preamble.bytes.size ());
	hashables.hash (hash_a);
}

uint64_t celerix::state_block::block_work () const
{
	return work;
}

void celerix::state_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

std::optional<celerix::block_hash> celerix::state_block::previous_field () const
{
	return hashables.previous;
}

std::optional<celerix::account> celerix::state_block::account_field () const
{
	return hashables.account;
}

void celerix::state_block::serialize (celerix::stream & stream_a) const
{
	write (stream_a, hashables.account);
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.balance);
	write (stream_a, hashables.link);
	write (stream_a, signature);
	write (stream_a, boost::endian::native_to_big (work));
}

bool celerix::state_block::deserialize (celerix::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.account);
		read (stream_a, hashables.previous);
		read (stream_a, hashables.representative);
		read (stream_a, hashables.balance);
		read (stream_a, hashables.link);
		read (stream_a, signature);
		read (stream_a, work);
		boost::endian::big_to_native_inplace (work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void celerix::state_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void celerix::state_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "state");
	tree.put ("account", hashables.account.to_account ());
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", hashables.representative.to_account ());
	tree.put ("balance", hashables.balance.to_string_dec ());
	tree.put ("link", hashables.link.to_string ());
	tree.put ("link_as_account", hashables.link.to_account ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
	tree.put ("work", celerix::to_string_hex (work));
}

bool celerix::state_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "state");
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto link_l (tree_a.get<std::string> ("link"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.account.decode_account (account_l);
		if (!error)
		{
			error = hashables.previous.decode_hex (previous_l);
			if (!error)
			{
				error = hashables.representative.decode_account (representative_l);
				if (!error)
				{
					error = hashables.balance.decode_dec (balance_l);
					if (!error)
					{
						error = hashables.link.decode_account (link_l) && hashables.link.decode_hex (link_l);
						if (!error)
						{
							error = celerix::from_string_hex (work_l, work);
							if (!error)
							{
								error = signature.decode_hex (signature_l);
							}
						}
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void celerix::state_block::visit (celerix::block_visitor & visitor_a) const
{
	visitor_a.state_block (*this);
}

void celerix::state_block::visit (celerix::mutable_block_visitor & visitor_a)
{
	visitor_a.state_block (*this);
}

celerix::block_type celerix::state_block::type () const
{
	return celerix::block_type::state;
}

bool celerix::state_block::operator== (celerix::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool celerix::state_block::operator== (celerix::state_block const & other_a) const
{
	return hashables.account == other_a.hashables.account && hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && hashables.balance == other_a.hashables.balance && hashables.link == other_a.hashables.link && signature == other_a.signature && work == other_a.work;
}

bool celerix::state_block::valid_predecessor (celerix::block const & block_a) const
{
	return true;
}

celerix::root celerix::state_block::root () const
{
	if (!hashables.previous.is_zero ())
	{
		return hashables.previous;
	}
	else
	{
		return hashables.account;
	}
}

std::optional<celerix::link> celerix::state_block::link_field () const
{
	return hashables.link;
}

std::optional<celerix::account> celerix::state_block::representative_field () const
{
	return hashables.representative;
}

std::optional<celerix::amount> celerix::state_block::balance_field () const
{
	return hashables.balance;
}

celerix::signature const & celerix::state_block::block_signature () const
{
	return signature;
}

void celerix::state_block::signature_set (celerix::signature const & signature_a)
{
	signature = signature_a;
}

void celerix::state_block::operator() (celerix::object_stream & obs) const
{
	celerix::block::operator() (obs); // Write common data

	obs.write ("account", hashables.account);
	obs.write ("previous", hashables.previous);
	obs.write ("representative", hashables.representative);
	obs.write ("balance", hashables.balance);
	obs.write ("link", hashables.link);
	obs.write ("signature", signature);
	obs.write ("work", work);
}

/*
 *
 */

std::shared_ptr<celerix::block> celerix::deserialize_block_json (boost::property_tree::ptree const & tree_a, celerix::block_uniquer * uniquer_a)
{
	std::shared_ptr<celerix::block> result;
	try
	{
		auto type (tree_a.get<std::string> ("type"));
		bool error (false);
		std::unique_ptr<celerix::block> obj;
		if (type == "receive")
		{
			obj = std::make_unique<celerix::receive_block> (error, tree_a);
		}
		else if (type == "send")
		{
			obj = std::make_unique<celerix::send_block> (error, tree_a);
		}
		else if (type == "open")
		{
			obj = std::make_unique<celerix::open_block> (error, tree_a);
		}
		else if (type == "change")
		{
			obj = std::make_unique<celerix::change_block> (error, tree_a);
		}
		else if (type == "state")
		{
			obj = std::make_unique<celerix::state_block> (error, tree_a);
		}

		if (!error)
		{
			result = std::move (obj);
		}
	}
	catch (std::runtime_error const &)
	{
	}
	if (uniquer_a != nullptr)
	{
		result = uniquer_a->unique (result);
	}
	return result;
}

void celerix::serialize_block (celerix::stream & stream_a, celerix::block const & block_a)
{
	celerix::write (stream_a, block_a.type ());
	block_a.serialize (stream_a);
}

std::shared_ptr<celerix::block> celerix::deserialize_block (celerix::stream & stream_a)
{
	celerix::block_type type;
	auto error (try_read (stream_a, type));
	std::shared_ptr<celerix::block> result;
	if (!error)
	{
		result = celerix::deserialize_block (stream_a, type);
	}
	return result;
}

std::shared_ptr<celerix::block> celerix::deserialize_block (celerix::stream & stream_a, celerix::block_type type_a, celerix::block_uniquer * uniquer_a)
{
	std::shared_ptr<celerix::block> result;
	switch (type_a)
	{
		case celerix::block_type::receive:
		{
			result = ::deserialize_block<celerix::receive_block> (stream_a);
			break;
		}
		case celerix::block_type::send:
		{
			result = ::deserialize_block<celerix::send_block> (stream_a);
			break;
		}
		case celerix::block_type::open:
		{
			result = ::deserialize_block<celerix::open_block> (stream_a);
			break;
		}
		case celerix::block_type::change:
		{
			result = ::deserialize_block<celerix::change_block> (stream_a);
			break;
		}
		case celerix::block_type::state:
		{
			result = ::deserialize_block<celerix::state_block> (stream_a);
			break;
		}
		default:
		{
			return {};
		}
	}
	if (result && uniquer_a != nullptr)
	{
		result = uniquer_a->unique (result);
	}
	return result;
}

/*
 * receive_block
 */

void celerix::receive_block::visit (celerix::block_visitor & visitor_a) const
{
	visitor_a.receive_block (*this);
}

void celerix::receive_block::visit (celerix::mutable_block_visitor & visitor_a)
{
	visitor_a.receive_block (*this);
}

bool celerix::receive_block::operator== (celerix::receive_block const & other_a) const
{
	auto result (hashables.previous == other_a.hashables.previous && hashables.source == other_a.hashables.source && work == other_a.work && signature == other_a.signature);
	return result;
}

void celerix::receive_block::serialize (celerix::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.source.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

bool celerix::receive_block::deserialize (celerix::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous.bytes);
		read (stream_a, hashables.source.bytes);
		read (stream_a, signature.bytes);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void celerix::receive_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void celerix::receive_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "receive");
	std::string previous;
	hashables.previous.encode_hex (previous);
	tree.put ("previous", previous);
	std::string source;
	hashables.source.encode_hex (source);
	tree.put ("source", source);
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", celerix::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool celerix::receive_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "receive");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.source.decode_hex (source_l);
			if (!error)
			{
				error = celerix::from_string_hex (work_l, work);
				if (!error)
				{
					error = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

celerix::receive_block::receive_block (celerix::block_hash const & previous_a, celerix::block_hash const & source_a, celerix::raw_key const & prv_a, celerix::public_key const & pub_a, uint64_t work_a) :
	hashables (previous_a, source_a),
	signature (celerix::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (pub_a != nullptr);
}

celerix::receive_block::receive_block (bool & error_a, celerix::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			celerix::read (stream_a, signature);
			celerix::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

celerix::receive_block::receive_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = celerix::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

std::shared_ptr<celerix::block> celerix::receive_block::clone () const
{
	return std::make_shared<celerix::receive_block> (*this);
}

void celerix::receive_block::generate_hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t celerix::receive_block::block_work () const
{
	return work;
}

void celerix::receive_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

bool celerix::receive_block::operator== (celerix::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool celerix::receive_block::valid_predecessor (celerix::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case celerix::block_type::send:
		case celerix::block_type::receive:
		case celerix::block_type::open:
		case celerix::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

std::optional<celerix::block_hash> celerix::receive_block::previous_field () const
{
	return hashables.previous;
}

std::optional<celerix::block_hash> celerix::receive_block::source_field () const
{
	return hashables.source;
}

celerix::root celerix::receive_block::root () const
{
	return hashables.previous;
}

celerix::signature const & celerix::receive_block::block_signature () const
{
	return signature;
}

void celerix::receive_block::signature_set (celerix::signature const & signature_a)
{
	signature = signature_a;
}

celerix::block_type celerix::receive_block::type () const
{
	return celerix::block_type::receive;
}

celerix::receive_hashables::receive_hashables (celerix::block_hash const & previous_a, celerix::block_hash const & source_a) :
	previous (previous_a),
	source (source_a)
{
}

celerix::receive_hashables::receive_hashables (bool & error_a, celerix::stream & stream_a)
{
	try
	{
		celerix::read (stream_a, previous.bytes);
		celerix::read (stream_a, source.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

celerix::receive_hashables::receive_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = source.decode_hex (source_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void celerix::receive_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
}

void celerix::receive_block::operator() (celerix::object_stream & obs) const
{
	celerix::block::operator() (obs); // Write common data

	obs.write ("previous", hashables.previous);
	obs.write ("source", hashables.source);
	obs.write ("signature", signature);
	obs.write ("work", work);
}
