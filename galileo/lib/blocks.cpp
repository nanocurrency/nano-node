#include <galileo/lib/blocks.hpp>

#include <boost/endian/conversion.hpp>

/** Compare blocks, first by type, then content. This is an optimization over dynamic_cast, which is very slow on some platforms. */
namespace
{
template <typename T>
bool blocks_equal (T const & first, galileo::block const & second)
{
	static_assert (std::is_base_of<galileo::block, T>::value, "Input parameter is not a block type");
	return (first.type () == second.type ()) && (static_cast<T const &> (second)) == first;
}
}

std::string galileo::to_string_hex (uint64_t value_a)
{
	std::stringstream stream;
	stream << std::hex << std::noshowbase << std::setw (16) << std::setfill ('0');
	stream << value_a;
	return stream.str ();
}

bool galileo::from_string_hex (std::string const & value_a, uint64_t & target_a)
{
	auto error (value_a.empty ());
	if (!error)
	{
		error = value_a.size () > 16;
		if (!error)
		{
			std::stringstream stream (value_a);
			stream << std::hex << std::noshowbase;
			try
			{
				uint64_t number_l;
				stream >> number_l;
				target_a = number_l;
				if (!stream.eof ())
				{
					error = true;
				}
			}
			catch (std::runtime_error &)
			{
				error = true;
			}
		}
	}
	return error;
}

std::string galileo::block::to_json ()
{
	std::string result;
	serialize_json (result);
	return result;
}

galileo::block_hash galileo::block::hash () const
{
	galileo::uint256_union result;
	blake2b_state hash_l;
	auto status (blake2b_init (&hash_l, sizeof (result.bytes)));
	assert (status == 0);
	hash (hash_l);
	status = blake2b_final (&hash_l, result.bytes.data (), sizeof (result.bytes));
	assert (status == 0);
	return result;
}

void galileo::send_block::visit (galileo::block_visitor & visitor_a) const
{
	visitor_a.send_block (*this);
}

void galileo::send_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t galileo::send_block::block_work () const
{
	return work;
}

void galileo::send_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

galileo::send_hashables::send_hashables (galileo::block_hash const & previous_a, galileo::account const & destination_a, galileo::amount const & balance_a) :
previous (previous_a),
destination (destination_a),
balance (balance_a)
{
}

galileo::send_hashables::send_hashables (bool & error_a, galileo::stream & stream_a)
{
	error_a = galileo::read (stream_a, previous.bytes);
	if (!error_a)
	{
		error_a = galileo::read (stream_a, destination.bytes);
		if (!error_a)
		{
			error_a = galileo::read (stream_a, balance.bytes);
		}
	}
}

galileo::send_hashables::send_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
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

void galileo::send_hashables::hash (blake2b_state & hash_a) const
{
	auto status (blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes)));
	assert (status == 0);
	status = blake2b_update (&hash_a, destination.bytes.data (), sizeof (destination.bytes));
	assert (status == 0);
	status = blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	assert (status == 0);
}

void galileo::send_block::serialize (galileo::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.destination.bytes);
	write (stream_a, hashables.balance.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

void galileo::send_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
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
	tree.put ("work", galileo::to_string_hex (work));
	tree.put ("signature", signature_l);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

bool galileo::send_block::deserialize (galileo::stream & stream_a)
{
	auto error (false);
	error = read (stream_a, hashables.previous.bytes);
	if (!error)
	{
		error = read (stream_a, hashables.destination.bytes);
		if (!error)
		{
			error = read (stream_a, hashables.balance.bytes);
			if (!error)
			{
				error = read (stream_a, signature.bytes);
				if (!error)
				{
					error = read (stream_a, work);
				}
			}
		}
	}
	return error;
}

bool galileo::send_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "send");
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
					error = galileo::from_string_hex (work_l, work);
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

galileo::send_block::send_block (galileo::block_hash const & previous_a, galileo::account const & destination_a, galileo::amount const & balance_a, galileo::raw_key const & prv_a, galileo::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, destination_a, balance_a),
signature (galileo::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

galileo::send_block::send_block (bool & error_a, galileo::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		error_a = galileo::read (stream_a, signature.bytes);
		if (!error_a)
		{
			error_a = galileo::read (stream_a, work);
		}
	}
}

galileo::send_block::send_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
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
				error_a = galileo::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

bool galileo::send_block::operator== (galileo::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool galileo::send_block::valid_predecessor (galileo::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case galileo::block_type::send:
		case galileo::block_type::receive:
		case galileo::block_type::open:
		case galileo::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

galileo::block_type galileo::send_block::type () const
{
	return galileo::block_type::send;
}

bool galileo::send_block::operator== (galileo::send_block const & other_a) const
{
	auto result (hashables.destination == other_a.hashables.destination && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance && work == other_a.work && signature == other_a.signature);
	return result;
}

galileo::block_hash galileo::send_block::previous () const
{
	return hashables.previous;
}

galileo::block_hash galileo::send_block::source () const
{
	return 0;
}

galileo::block_hash galileo::send_block::root () const
{
	return hashables.previous;
}

galileo::account galileo::send_block::representative () const
{
	return 0;
}

galileo::signature galileo::send_block::block_signature () const
{
	return signature;
}

void galileo::send_block::signature_set (galileo::uint512_union const & signature_a)
{
	signature = signature_a;
}

galileo::open_hashables::open_hashables (galileo::block_hash const & source_a, galileo::account const & representative_a, galileo::account const & account_a) :
source (source_a),
representative (representative_a),
account (account_a)
{
}

galileo::open_hashables::open_hashables (bool & error_a, galileo::stream & stream_a)
{
	error_a = galileo::read (stream_a, source.bytes);
	if (!error_a)
	{
		error_a = galileo::read (stream_a, representative.bytes);
		if (!error_a)
		{
			error_a = galileo::read (stream_a, account.bytes);
		}
	}
}

galileo::open_hashables::open_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
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

void galileo::open_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
}

galileo::open_block::open_block (galileo::block_hash const & source_a, galileo::account const & representative_a, galileo::account const & account_a, galileo::raw_key const & prv_a, galileo::public_key const & pub_a, uint64_t work_a) :
hashables (source_a, representative_a, account_a),
signature (galileo::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
	assert (!representative_a.is_zero ());
}

galileo::open_block::open_block (galileo::block_hash const & source_a, galileo::account const & representative_a, galileo::account const & account_a, std::nullptr_t) :
hashables (source_a, representative_a, account_a),
work (0)
{
	signature.clear ();
}

galileo::open_block::open_block (bool & error_a, galileo::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		error_a = galileo::read (stream_a, signature);
		if (!error_a)
		{
			error_a = galileo::read (stream_a, work);
		}
	}
}

galileo::open_block::open_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = galileo::from_string_hex (work_l, work);
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

void galileo::open_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t galileo::open_block::block_work () const
{
	return work;
}

void galileo::open_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

galileo::block_hash galileo::open_block::previous () const
{
	galileo::block_hash result (0);
	return result;
}

void galileo::open_block::serialize (galileo::stream & stream_a) const
{
	write (stream_a, hashables.source);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.account);
	write (stream_a, signature);
	write (stream_a, work);
}

void galileo::open_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	tree.put ("type", "open");
	tree.put ("source", hashables.source.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("account", hashables.account.to_account ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", galileo::to_string_hex (work));
	tree.put ("signature", signature_l);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

bool galileo::open_block::deserialize (galileo::stream & stream_a)
{
	auto error (read (stream_a, hashables.source));
	if (!error)
	{
		error = read (stream_a, hashables.representative);
		if (!error)
		{
			error = read (stream_a, hashables.account);
			if (!error)
			{
				error = read (stream_a, signature);
				if (!error)
				{
					error = read (stream_a, work);
				}
			}
		}
	}
	return error;
}

bool galileo::open_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "open");
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
					error = galileo::from_string_hex (work_l, work);
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

void galileo::open_block::visit (galileo::block_visitor & visitor_a) const
{
	visitor_a.open_block (*this);
}

galileo::block_type galileo::open_block::type () const
{
	return galileo::block_type::open;
}

bool galileo::open_block::operator== (galileo::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool galileo::open_block::operator== (galileo::open_block const & other_a) const
{
	return hashables.source == other_a.hashables.source && hashables.representative == other_a.hashables.representative && hashables.account == other_a.hashables.account && work == other_a.work && signature == other_a.signature;
}

bool galileo::open_block::valid_predecessor (galileo::block const & block_a) const
{
	return false;
}

galileo::block_hash galileo::open_block::source () const
{
	return hashables.source;
}

galileo::block_hash galileo::open_block::root () const
{
	return hashables.account;
}

galileo::account galileo::open_block::representative () const
{
	return hashables.representative;
}

galileo::signature galileo::open_block::block_signature () const
{
	return signature;
}

void galileo::open_block::signature_set (galileo::uint512_union const & signature_a)
{
	signature = signature_a;
}

galileo::change_hashables::change_hashables (galileo::block_hash const & previous_a, galileo::account const & representative_a) :
previous (previous_a),
representative (representative_a)
{
}

galileo::change_hashables::change_hashables (bool & error_a, galileo::stream & stream_a)
{
	error_a = galileo::read (stream_a, previous);
	if (!error_a)
	{
		error_a = galileo::read (stream_a, representative);
	}
}

galileo::change_hashables::change_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
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

void galileo::change_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
}

galileo::change_block::change_block (galileo::block_hash const & previous_a, galileo::account const & representative_a, galileo::raw_key const & prv_a, galileo::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, representative_a),
signature (galileo::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

galileo::change_block::change_block (bool & error_a, galileo::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		error_a = galileo::read (stream_a, signature);
		if (!error_a)
		{
			error_a = galileo::read (stream_a, work);
		}
	}
}

galileo::change_block::change_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = galileo::from_string_hex (work_l, work);
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

void galileo::change_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t galileo::change_block::block_work () const
{
	return work;
}

void galileo::change_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

galileo::block_hash galileo::change_block::previous () const
{
	return hashables.previous;
}

void galileo::change_block::serialize (galileo::stream & stream_a) const
{
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, signature);
	write (stream_a, work);
}

void galileo::change_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	tree.put ("type", "change");
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("work", galileo::to_string_hex (work));
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

bool galileo::change_block::deserialize (galileo::stream & stream_a)
{
	auto error (read (stream_a, hashables.previous));
	if (!error)
	{
		error = read (stream_a, hashables.representative);
		if (!error)
		{
			error = read (stream_a, signature);
			if (!error)
			{
				error = read (stream_a, work);
			}
		}
	}
	return error;
}

bool galileo::change_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "change");
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
				error = galileo::from_string_hex (work_l, work);
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

void galileo::change_block::visit (galileo::block_visitor & visitor_a) const
{
	visitor_a.change_block (*this);
}

galileo::block_type galileo::change_block::type () const
{
	return galileo::block_type::change;
}

bool galileo::change_block::operator== (galileo::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool galileo::change_block::operator== (galileo::change_block const & other_a) const
{
	return hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && work == other_a.work && signature == other_a.signature;
}

bool galileo::change_block::valid_predecessor (galileo::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case galileo::block_type::send:
		case galileo::block_type::receive:
		case galileo::block_type::open:
		case galileo::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

galileo::block_hash galileo::change_block::source () const
{
	return 0;
}

galileo::block_hash galileo::change_block::root () const
{
	return hashables.previous;
}

galileo::account galileo::change_block::representative () const
{
	return hashables.representative;
}

galileo::signature galileo::change_block::block_signature () const
{
	return signature;
}

void galileo::change_block::signature_set (galileo::uint512_union const & signature_a)
{
	signature = signature_a;
}

galileo::state_hashables::state_hashables (galileo::account const & account_a, galileo::block_hash const & previous_a, galileo::account const & representative_a, galileo::amount const & balance_a, galileo::uint256_union const & link_a) :
account (account_a),
previous (previous_a),
representative (representative_a),
balance (balance_a),
link (link_a)
{
}

galileo::state_hashables::state_hashables (bool & error_a, galileo::stream & stream_a)
{
	error_a = galileo::read (stream_a, account);
	if (!error_a)
	{
		error_a = galileo::read (stream_a, previous);
		if (!error_a)
		{
			error_a = galileo::read (stream_a, representative);
			if (!error_a)
			{
				error_a = galileo::read (stream_a, balance);
				if (!error_a)
				{
					error_a = galileo::read (stream_a, link);
				}
			}
		}
	}
}

galileo::state_hashables::state_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
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

void galileo::state_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	blake2b_update (&hash_a, link.bytes.data (), sizeof (link.bytes));
}

galileo::state_block::state_block (galileo::account const & account_a, galileo::block_hash const & previous_a, galileo::account const & representative_a, galileo::amount const & balance_a, galileo::uint256_union const & link_a, galileo::raw_key const & prv_a, galileo::public_key const & pub_a, uint64_t work_a) :
hashables (account_a, previous_a, representative_a, balance_a, link_a),
signature (galileo::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

galileo::state_block::state_block (bool & error_a, galileo::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		error_a = galileo::read (stream_a, signature);
		if (!error_a)
		{
			error_a = galileo::read (stream_a, work);
			boost::endian::big_to_native_inplace (work);
		}
	}
}

galileo::state_block::state_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
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
				error_a = galileo::from_string_hex (work_l, work);
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

void galileo::state_block::hash (blake2b_state & hash_a) const
{
	galileo::uint256_union preamble (static_cast<uint64_t> (galileo::block_type::state));
	blake2b_update (&hash_a, preamble.bytes.data (), preamble.bytes.size ());
	hashables.hash (hash_a);
}

uint64_t galileo::state_block::block_work () const
{
	return work;
}

void galileo::state_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

galileo::block_hash galileo::state_block::previous () const
{
	return hashables.previous;
}

void galileo::state_block::serialize (galileo::stream & stream_a) const
{
	write (stream_a, hashables.account);
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.balance);
	write (stream_a, hashables.link);
	write (stream_a, signature);
	write (stream_a, boost::endian::native_to_big (work));
}

void galileo::state_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	tree.put ("type", "state");
	tree.put ("account", hashables.account.to_account ());
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("balance", hashables.balance.to_string_dec ());
	tree.put ("link", hashables.link.to_string ());
	tree.put ("link_as_account", hashables.link.to_account ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
	tree.put ("work", galileo::to_string_hex (work));
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

bool galileo::state_block::deserialize (galileo::stream & stream_a)
{
	auto error (read (stream_a, hashables.account));
	if (!error)
	{
		error = read (stream_a, hashables.previous);
		if (!error)
		{
			error = read (stream_a, hashables.representative);
			if (!error)
			{
				error = read (stream_a, hashables.balance);
				if (!error)
				{
					error = read (stream_a, hashables.link);
					if (!error)
					{
						error = read (stream_a, signature);
						if (!error)
						{
							error = read (stream_a, work);
							boost::endian::big_to_native_inplace (work);
						}
					}
				}
			}
		}
	}
	return error;
}

bool galileo::state_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "state");
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
							error = galileo::from_string_hex (work_l, work);
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

void galileo::state_block::visit (galileo::block_visitor & visitor_a) const
{
	visitor_a.state_block (*this);
}

galileo::block_type galileo::state_block::type () const
{
	return galileo::block_type::state;
}

bool galileo::state_block::operator== (galileo::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool galileo::state_block::operator== (galileo::state_block const & other_a) const
{
	return hashables.account == other_a.hashables.account && hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && hashables.balance == other_a.hashables.balance && hashables.link == other_a.hashables.link && signature == other_a.signature && work == other_a.work;
}

bool galileo::state_block::valid_predecessor (galileo::block const & block_a) const
{
	return true;
}

galileo::block_hash galileo::state_block::source () const
{
	return 0;
}

galileo::block_hash galileo::state_block::root () const
{
	return !hashables.previous.is_zero () ? hashables.previous : hashables.account;
}

galileo::account galileo::state_block::representative () const
{
	return hashables.representative;
}

galileo::signature galileo::state_block::block_signature () const
{
	return signature;
}

void galileo::state_block::signature_set (galileo::uint512_union const & signature_a)
{
	signature = signature_a;
}

std::unique_ptr<galileo::block> galileo::deserialize_block_json (boost::property_tree::ptree const & tree_a)
{
	std::unique_ptr<galileo::block> result;
	try
	{
		auto type (tree_a.get<std::string> ("type"));
		if (type == "receive")
		{
			bool error (false);
			std::unique_ptr<galileo::receive_block> obj (new galileo::receive_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "send")
		{
			bool error (false);
			std::unique_ptr<galileo::send_block> obj (new galileo::send_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "open")
		{
			bool error (false);
			std::unique_ptr<galileo::open_block> obj (new galileo::open_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "change")
		{
			bool error (false);
			std::unique_ptr<galileo::change_block> obj (new galileo::change_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "state")
		{
			bool error (false);
			std::unique_ptr<galileo::state_block> obj (new galileo::state_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
	}
	catch (std::runtime_error const &)
	{
	}
	return result;
}

std::unique_ptr<galileo::block> galileo::deserialize_block (galileo::stream & stream_a)
{
	galileo::block_type type;
	auto error (read (stream_a, type));
	std::unique_ptr<galileo::block> result;
	if (!error)
	{
		result = galileo::deserialize_block (stream_a, type);
	}
	return result;
}

std::unique_ptr<galileo::block> galileo::deserialize_block (galileo::stream & stream_a, galileo::block_type type_a)
{
	std::unique_ptr<galileo::block> result;
	switch (type_a)
	{
		case galileo::block_type::receive:
		{
			bool error (false);
			std::unique_ptr<galileo::receive_block> obj (new galileo::receive_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		case galileo::block_type::send:
		{
			bool error (false);
			std::unique_ptr<galileo::send_block> obj (new galileo::send_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		case galileo::block_type::open:
		{
			bool error (false);
			std::unique_ptr<galileo::open_block> obj (new galileo::open_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		case galileo::block_type::change:
		{
			bool error (false);
			std::unique_ptr<galileo::change_block> obj (new galileo::change_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		case galileo::block_type::state:
		{
			bool error (false);
			std::unique_ptr<galileo::state_block> obj (new galileo::state_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		default:
			assert (false);
			break;
	}
	return result;
}

void galileo::receive_block::visit (galileo::block_visitor & visitor_a) const
{
	visitor_a.receive_block (*this);
}

bool galileo::receive_block::operator== (galileo::receive_block const & other_a) const
{
	auto result (hashables.previous == other_a.hashables.previous && hashables.source == other_a.hashables.source && work == other_a.work && signature == other_a.signature);
	return result;
}

bool galileo::receive_block::deserialize (galileo::stream & stream_a)
{
	auto error (false);
	error = read (stream_a, hashables.previous.bytes);
	if (!error)
	{
		error = read (stream_a, hashables.source.bytes);
		if (!error)
		{
			error = read (stream_a, signature.bytes);
			if (!error)
			{
				error = read (stream_a, work);
			}
		}
	}
	return error;
}

bool galileo::receive_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "receive");
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
				error = galileo::from_string_hex (work_l, work);
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

void galileo::receive_block::serialize (galileo::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.source.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

void galileo::receive_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	tree.put ("type", "receive");
	std::string previous;
	hashables.previous.encode_hex (previous);
	tree.put ("previous", previous);
	std::string source;
	hashables.source.encode_hex (source);
	tree.put ("source", source);
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", galileo::to_string_hex (work));
	tree.put ("signature", signature_l);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

galileo::receive_block::receive_block (galileo::block_hash const & previous_a, galileo::block_hash const & source_a, galileo::raw_key const & prv_a, galileo::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, source_a),
signature (galileo::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

galileo::receive_block::receive_block (bool & error_a, galileo::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		error_a = galileo::read (stream_a, signature);
		if (!error_a)
		{
			error_a = galileo::read (stream_a, work);
		}
	}
}

galileo::receive_block::receive_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
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
				error_a = galileo::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void galileo::receive_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t galileo::receive_block::block_work () const
{
	return work;
}

void galileo::receive_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

bool galileo::receive_block::operator== (galileo::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool galileo::receive_block::valid_predecessor (galileo::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case galileo::block_type::send:
		case galileo::block_type::receive:
		case galileo::block_type::open:
		case galileo::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

galileo::block_hash galileo::receive_block::previous () const
{
	return hashables.previous;
}

galileo::block_hash galileo::receive_block::source () const
{
	return hashables.source;
}

galileo::block_hash galileo::receive_block::root () const
{
	return hashables.previous;
}

galileo::account galileo::receive_block::representative () const
{
	return 0;
}

galileo::signature galileo::receive_block::block_signature () const
{
	return signature;
}

void galileo::receive_block::signature_set (galileo::uint512_union const & signature_a)
{
	signature = signature_a;
}

galileo::block_type galileo::receive_block::type () const
{
	return galileo::block_type::receive;
}

galileo::receive_hashables::receive_hashables (galileo::block_hash const & previous_a, galileo::block_hash const & source_a) :
previous (previous_a),
source (source_a)
{
}

galileo::receive_hashables::receive_hashables (bool & error_a, galileo::stream & stream_a)
{
	error_a = galileo::read (stream_a, previous.bytes);
	if (!error_a)
	{
		error_a = galileo::read (stream_a, source.bytes);
	}
}

galileo::receive_hashables::receive_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
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

void galileo::receive_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
}
