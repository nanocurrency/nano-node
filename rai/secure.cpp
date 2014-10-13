#include <rai/secure.hpp>

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>

void rai::uint256_union::digest_password (std::string const & password_a)
{
	CryptoPP::SHA3 hash (32);
	hash.Update (reinterpret_cast <uint8_t const *> (password_a.c_str ()), password_a.size ());
	hash.Final (bytes.data ());
}

void rai::votes::vote (rai::vote const & vote_a)
{
	if (!rai::validate_message (vote_a.address, vote_a.hash (), vote_a.signature))
	{
		auto existing (rep_votes.find (vote_a.address));
		if (existing == rep_votes.end ())
		{
			rep_votes.insert (std::make_pair (vote_a.address, std::make_pair (vote_a.sequence, vote_a.block->clone ())));
		}
		else
		{
			if (existing->second.first < vote_a.sequence)
			{
				existing->second.second = vote_a.block->clone ();
			}
		}
		assert (rep_votes.size () > 0);
		auto winner_l (winner ());
		if (winner_l.second > flip_threshold ())
		{
			if (!(*winner_l.first == *last_winner))
			{
				ledger.rollback (last_winner->hash ());
				ledger.process (*winner_l.first);
				last_winner = std::move (winner_l.first);
			}
		}
	}
}

std::pair <std::unique_ptr <rai::block>, rai::uint256_t> rai::votes::winner ()
{
	std::unordered_map <rai::block_hash, std::pair <std::unique_ptr <block>, rai::uint256_t>> totals;
	for (auto & i: rep_votes)
	{
		auto hash (i.second.second->hash ());
		auto existing (totals.find (hash));
		if (existing == totals.end ())
		{
			totals.insert (std::make_pair (hash, std::make_pair (i.second.second->clone (), 0)));
			existing = totals.find (hash);
		}
		auto weight (ledger.weight (i.first));
		existing->second.second += weight;
	}
	std::pair <std::unique_ptr <rai::block>, rai::uint256_t> winner_l;
	for (auto & i: totals)
	{
		if (i.second.second >= winner_l.second)
		{
			winner_l.first = i.second.first->clone ();
			winner_l.second = i.second.second;
		}
	}
	return winner_l;
}

rai::votes::votes (rai::ledger & ledger_a, rai::block const & block_a) :
ledger (ledger_a),
root (ledger.store.root (block_a)),
last_winner (block_a.clone ()),
sequence (0)
{
}

rai::keypair::keypair ()
{
	ed25519_randombytes_unsafe (prv.bytes.data (), sizeof (prv.bytes));
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

rai::keypair::keypair (std::string const & prv_a)
{
	auto error (prv.decode_hex (prv_a));
	assert (!error);
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

rai::ledger::ledger (rai::block_store & store_a) :
store (store_a)
{
	store.checksum_put (0, 0, 0);
}

bool rai::uint256_union::operator == (rai::uint256_union const & other_a) const
{
	return bytes == other_a.bytes;
}

bool rai::uint512_union::operator == (rai::uint512_union const & other_a) const
{
	return bytes == other_a.bytes;
}

void rai::uint256_union::serialize (rai::stream & stream_a) const
{
	write (stream_a, bytes);
}

bool rai::uint256_union::deserialize (rai::stream & stream_a)
{
	return read (stream_a, bytes);
}

rai::uint256_union::uint256_union (rai::private_key const & prv, uint256_union const & key, uint128_union const & iv)
{
	rai::uint256_union exponent (prv);
	CryptoPP::AES::Encryption alg (key.bytes.data (), sizeof (key.bytes));
	CryptoPP::CBC_Mode_ExternalCipher::Encryption enc (alg, iv.bytes.data ());
	enc.ProcessData (bytes.data (), exponent.bytes.data (), sizeof (exponent.bytes));
}

rai::private_key rai::uint256_union::prv (rai::secret_key const & key_a, uint128_union const & iv) const
{
	CryptoPP::AES::Decryption alg (key_a.bytes.data (), sizeof (key_a.bytes));
	CryptoPP::CBC_Mode_ExternalCipher::Decryption dec (alg, iv.bytes.data ());
	rai::private_key result;
	dec.ProcessData (result.bytes.data (), bytes.data (), sizeof (bytes));
	return result;
}

void rai::send_block::visit (rai::block_visitor & visitor_a) const
{
	visitor_a.send_block (*this);
}

void rai::receive_block::visit (rai::block_visitor & visitor_a) const
{
	visitor_a.receive_block (*this);
}

void rai::send_block::hash (CryptoPP::SHA3 & hash_a) const
{
	hashables.hash (hash_a);
}

void rai::send_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
	hash_a.Update (previous.bytes.data (), sizeof (previous.bytes));
	hash_a.Update (balance.bytes.data (), sizeof (balance.bytes));
	hash_a.Update (destination.bytes.data (), sizeof (destination.bytes));
}

void rai::send_block::serialize (rai::stream & stream_a) const
{
	write (stream_a, signature.bytes);
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.balance.bytes);
	write (stream_a, hashables.destination.bytes);
}

bool rai::send_block::deserialize (rai::stream & stream_a)
{
	auto result (false);
	result = read (stream_a, signature.bytes);
	if (!result)
	{
		result = read (stream_a, hashables.previous.bytes);
		if (!result)
		{
			result = read (stream_a, hashables.balance.bytes);
			if (!result)
			{
				result = read (stream_a, hashables.destination.bytes);
			}
		}
	}
	return result;
}

void rai::receive_block::sign (rai::private_key const & prv, rai::public_key const & pub, rai::uint256_union const & hash_a)
{
	sign_message (prv, pub, hash_a, signature);
}

bool rai::receive_block::operator == (rai::receive_block const & other_a) const
{
	auto result (signature == other_a.signature && hashables.previous == other_a.hashables.previous && hashables.source == other_a.hashables.source);
	return result;
}

bool rai::receive_block::deserialize (rai::stream & stream_a)
{
	auto result (false);
	result = read (stream_a, signature.bytes);
	if (!result)
	{
		result = read (stream_a, hashables.previous.bytes);
		if (!result)
		{
			result = read (stream_a, hashables.source.bytes);
		}
	}
	return result;
}

void rai::receive_block::serialize (rai::stream & stream_a) const
{
	write (stream_a, signature.bytes);
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.source.bytes);
}

void rai::receive_block::hash (CryptoPP::SHA3 & hash_a) const
{
	hashables.hash (hash_a);
}

void rai::receive_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
	hash_a.Update (source.bytes.data (), sizeof (source.bytes));
	hash_a.Update (previous.bytes.data (), sizeof (previous.bytes));
}