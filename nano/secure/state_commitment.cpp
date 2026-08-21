#include <nano/crypto/blake2/blake2.h>
#include <nano/secure/account_info.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_cemented.hpp>
#include <nano/secure/pending_info.hpp>
#include <nano/secure/state_commitment.hpp>
#include <nano/secure/transaction.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/confirmation_height.hpp>
#include <nano/store/ledger/pending.hpp>
#include <nano/store/ledger_store.hpp>

#include <array>
#include <cstring>
#include <vector>

namespace
{
// Domain-separation tags. Leaf and node prefixes differ so that no internal node
// hash can ever collide with a leaf hash (second-preimage resistance for the tree).
constexpr uint8_t leaf_prefix = 0x00;
constexpr uint8_t node_prefix = 0x01;

// Minimal blake2b-256 hashing helper mirroring nano::block::generate_hash.
class hasher final
{
public:
	hasher ()
	{
		auto status = blake2b_init (&state, sizeof (nano::block_hash::bytes));
		debug_assert (status == 0);
		(void)status;
	}
	void update (uint8_t const * data, size_t size)
	{
		auto status = blake2b_update (&state, data, size);
		debug_assert (status == 0);
		(void)status;
	}
	void update (uint8_t byte)
	{
		update (&byte, 1);
	}
	template <typename T>
	void update_union (T const & value)
	{
		update (value.bytes.data (), value.bytes.size ());
	}
	void update_u64 (uint64_t value)
	{
		// Fixed little-endian encoding so the digest is platform-independent.
		std::array<uint8_t, 8> buffer;
		for (auto i = 0; i < 8; ++i)
		{
			buffer[i] = static_cast<uint8_t> (value >> (8 * i));
		}
		update (buffer.data (), buffer.size ());
	}
	nano::block_hash finalize ()
	{
		nano::block_hash result;
		auto status = blake2b_final (&state, result.bytes.data (), sizeof (result.bytes));
		debug_assert (status == 0);
		(void)status;
		return result;
	}

private:
	blake2b_state state;
};

nano::block_hash hash_internal_node (nano::block_hash const & left, nano::block_hash const & right)
{
	hasher h;
	h.update (node_prefix);
	h.update_union (left);
	h.update_union (right);
	return h.finalize ();
}

/*
 * Streaming Merkle Mountain Range accumulator. Leaves are folded in one at a time
 * in canonical order; only the current peaks (at most log2(n) of them) are held in
 * memory. bag() collapses the peaks into a single root deterministically, folding
 * from the smallest peak up into the largest so that a fixed leaf sequence always
 * yields the same root.
 */
class merkle_mountain_range final
{
public:
	void add_leaf (nano::block_hash const & leaf)
	{
		nano::block_hash carry = leaf;
		size_t height = 0;
		while (height < peaks.size () && peaks[height].has_value ())
		{
			carry = hash_internal_node (peaks[height].value (), carry);
			peaks[height].reset ();
			++height;
		}
		if (height == peaks.size ())
		{
			peaks.emplace_back (carry);
		}
		else
		{
			peaks[height] = carry;
		}
		++leaf_count;
	}

	uint64_t count () const
	{
		return leaf_count;
	}

	// Deterministic root over all leaves added so far. Empty accumulator hashes to
	// a fixed sentinel (node hash of the empty peak set) so callers get a stable value.
	nano::block_hash bag () const
	{
		std::optional<nano::block_hash> accumulator;
		for (auto const & peak : peaks)
		{
			if (!peak.has_value ())
			{
				continue;
			}
			if (!accumulator.has_value ())
			{
				accumulator = peak.value ();
			}
			else
			{
				accumulator = hash_internal_node (peak.value (), accumulator.value ());
			}
		}
		if (!accumulator.has_value ())
		{
			// No leaves: return a fixed, domain-separated empty root.
			hasher h;
			h.update (node_prefix);
			nano::block_hash zero{ 0 };
			h.update_union (zero);
			h.update_union (zero);
			return h.finalize ();
		}
		return accumulator.value ();
	}

private:
	std::vector<std::optional<nano::block_hash>> peaks;
	uint64_t leaf_count{ 0 };
};

nano::block_hash account_leaf (nano::account const & account, nano::block_hash const & frontier, nano::amount const & balance, uint64_t height)
{
	hasher h;
	h.update (leaf_prefix);
	static constexpr std::array<uint8_t, 4> tag{ 'a', 'c', 'c', 't' };
	h.update (tag.data (), tag.size ());
	h.update_union (account);
	h.update_union (frontier);
	h.update_union (balance);
	h.update_u64 (height);
	return h.finalize ();
}

nano::block_hash pending_leaf (nano::pending_key const & key, nano::pending_info const & info)
{
	hasher h;
	h.update (leaf_prefix);
	static constexpr std::array<uint8_t, 4> tag{ 'p', 'e', 'n', 'd' };
	h.update (tag.data (), tag.size ());
	h.update_union (key.account);
	h.update_union (key.hash);
	h.update_union (info.source);
	h.update_union (info.amount);
	h.update (static_cast<uint8_t> (info.epoch));
	return h.finalize ();
}
}

nano::state_commitment_result nano::compute_state_commitment (nano::ledger const & ledger, nano::secure::transaction const & transaction)
{
	nano::state_commitment_result result;

	// Accounts: iterate the cemented frontier table in key order. Each account
	// contributes its cemented frontier hash, balance and height.
	merkle_mountain_range accounts_mmr;
	for (auto i = ledger.store.confirmation_height.begin (transaction), n = ledger.store.confirmation_height.end (transaction); i != n; ++i)
	{
		nano::account const & account = i->first;
		nano::confirmation_height_info const & info = i->second;
		if (info.height == 0)
		{
			continue;
		}
		auto balance = ledger.cemented.block_balance (transaction, info.frontier);
		if (!balance.has_value ())
		{
			// Frontier block not resolvable (e.g. pruned): skip rather than commit to a guess.
			continue;
		}
		accounts_mmr.add_leaf (account_leaf (account, info.frontier, balance.value (), info.height));
	}
	result.accounts_root = accounts_mmr.bag ();
	result.account_count = accounts_mmr.count ();

	// Pending: iterate the pending table in key order, including only entries whose
	// send block has been cemented, so the commitment reflects cemented state only.
	merkle_mountain_range pending_mmr;
	for (auto i = ledger.store.pending.begin (transaction), n = ledger.store.pending.end (transaction); i != n; ++i)
	{
		nano::pending_key const & key = i->first;
		nano::pending_info const & info = i->second;
		if (!ledger.cemented.block_exists_or_pruned (transaction, key.hash))
		{
			continue;
		}
		pending_mmr.add_leaf (pending_leaf (key, info));
	}
	result.pending_root = pending_mmr.bag ();
	result.pending_count = pending_mmr.count ();

	// Overall root binds both sub-roots plus their counts under a versioned tag.
	hasher h;
	static constexpr std::array<uint8_t, 25> tag{ 'n', 'a', 'n', 'o', '-', 's', 't', 'a', 't', 'e', '-', 'c', 'o', 'm', 'm', 'i', 't', 'm', 'e', 'n', 't', '-', 'v', '1', '\0' };
	h.update (tag.data (), tag.size ());
	h.update_union (result.accounts_root);
	h.update_union (result.pending_root);
	h.update_u64 (result.account_count);
	h.update_u64 (result.pending_count);
	result.root = h.finalize ();

	return result;
}
