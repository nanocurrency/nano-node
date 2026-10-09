#include <nano/crypto/blake2/blake2.h>
#include <nano/lib/block_sideband.hpp>
#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
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

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <optional>
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

// Fixed empty-tree value (domain-separated node hash of two zero hashes).
nano::block_hash empty_root ()
{
	hasher h;
	h.update (node_prefix);
	nano::block_hash zero{ 0 };
	h.update_union (zero);
	h.update_union (zero);
	return h.finalize ();
}

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

// Merkle Mountain Range root over an ordered leaf list. bag() folds the peaks from
// the smallest (height 0) upward, each peak on the left of the running accumulator,
// exactly as the streaming accumulator does, so a fixed leaf sequence yields a fixed
// root. This is the single source of truth for sub-tree roots.
nano::block_hash mmr_root (std::vector<nano::block_hash> const & leaves)
{
	std::vector<std::optional<nano::block_hash>> peaks;
	for (auto const & leaf : leaves)
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
	}
	std::optional<nano::block_hash> accumulator;
	for (auto const & peak : peaks)
	{
		if (!peak.has_value ())
		{
			continue;
		}
		accumulator = accumulator.has_value () ? hash_internal_node (peak.value (), accumulator.value ()) : peak.value ();
	}
	return accumulator.has_value () ? accumulator.value () : empty_root ();
}

// Overall root binding both sub-roots plus their counts under a versioned tag.
nano::block_hash overall_root (nano::block_hash const & accounts_root, nano::block_hash const & pending_root, uint64_t account_count, uint64_t pending_count)
{
	hasher h;
	static constexpr std::array<uint8_t, 25> tag{ 'n', 'a', 'n', 'o', '-', 's', 't', 'a', 't', 'e', '-', 'c', 'o', 'm', 'm', 'i', 't', 'm', 'e', 'n', 't', '-', 'v', '1', '\0' };
	h.update (tag.data (), tag.size ());
	h.update_union (accounts_root);
	h.update_union (pending_root);
	h.update_u64 (account_count);
	h.update_u64 (pending_count);
	return h.finalize ();
}

// The ordered cemented leaves plus their keys, so callers can find a target index.
struct collected_leaves final
{
	std::vector<nano::account> account_keys;
	std::vector<nano::block_hash> account_leaves;
	std::vector<nano::pending_key> pending_keys;
	std::vector<nano::block_hash> pending_leaves;
};

collected_leaves collect (nano::ledger const & ledger, nano::secure::transaction const & transaction)
{
	collected_leaves out;
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
			continue;
		}
		out.account_keys.push_back (account);
		out.account_leaves.push_back (account_leaf (account, info.frontier, balance.value (), info.height));
	}
	for (auto i = ledger.store.pending.begin (transaction), n = ledger.store.pending.end (transaction); i != n; ++i)
	{
		nano::pending_key const & key = i->first;
		nano::pending_info const & info = i->second;
		if (!ledger.cemented.block_exists_or_pruned (transaction, key.hash))
		{
			continue;
		}
		out.pending_keys.push_back (key);
		out.pending_leaves.push_back (pending_leaf (key, info));
	}
	return out;
}

// Build the MMR inclusion path + peaks for leaf `target` within an ordered list.
// `peaks` are returned in ascending-height (bag consumption) order; `peak_index` is
// the position in that list of the mountain the target belongs to.
void build_mmr_proof (std::vector<nano::block_hash> const & leaves, uint64_t target, std::vector<nano::state_proof_step> & path, std::vector<nano::block_hash> & peaks, uint64_t & peak_index)
{
	uint64_t n = leaves.size ();
	// Perfect subtrees (mountains), largest height first, covering earliest leaves.
	struct mountain
	{
		uint64_t start;
		uint64_t size;
	};
	std::vector<mountain> mountains;
	uint64_t cursor = 0;
	for (int height = 62; height >= 0; --height)
	{
		uint64_t const bit = uint64_t{ 1 } << height;
		if (n & bit)
		{
			mountains.push_back ({ cursor, bit });
			cursor += bit;
		}
	}
	peaks.assign (mountains.size (), nano::block_hash{ 0 });
	size_t target_desc_index = 0;
	for (size_t m = 0; m < mountains.size (); ++m)
	{
		auto const & mount = mountains[m];
		bool const holds = target >= mount.start && target < mount.start + mount.size;
		std::vector<nano::block_hash> level (leaves.begin () + mount.start, leaves.begin () + mount.start + mount.size);
		uint64_t pos = holds ? target - mount.start : 0;
		while (level.size () > 1)
		{
			if (holds)
			{
				uint64_t const sibling = pos ^ 1;
				path.push_back ({ level[sibling], sibling == pos + 1 });
			}
			std::vector<nano::block_hash> next (level.size () / 2);
			for (size_t j = 0; j < next.size (); ++j)
			{
				next[j] = hash_internal_node (level[2 * j], level[2 * j + 1]);
			}
			level = std::move (next);
			pos /= 2;
		}
		// bag() consumes peaks ascending-height => reverse of the descending mountain order.
		size_t const ascending = mountains.size () - 1 - m;
		peaks[ascending] = level[0];
		if (holds)
		{
			target_desc_index = m;
		}
	}
	peak_index = mountains.size () - 1 - target_desc_index;
}
}

nano::state_commitment_result nano::compute_state_commitment (nano::ledger const & ledger, nano::secure::transaction const & transaction)
{
	auto leaves = collect (ledger, transaction);
	nano::state_commitment_result result;
	result.account_count = leaves.account_leaves.size ();
	result.pending_count = leaves.pending_leaves.size ();
	result.accounts_root = mmr_root (leaves.account_leaves);
	result.pending_root = mmr_root (leaves.pending_leaves);
	result.root = overall_root (result.accounts_root, result.pending_root, result.account_count, result.pending_count);
	return result;
}

std::optional<nano::state_proof> nano::generate_account_proof (nano::ledger const & ledger, nano::secure::transaction const & transaction, nano::account const & account)
{
	auto leaves = collect (ledger, transaction);
	auto it = std::find (leaves.account_keys.begin (), leaves.account_keys.end (), account);
	if (it == leaves.account_keys.end ())
	{
		return std::nullopt;
	}
	uint64_t const target = static_cast<uint64_t> (std::distance (leaves.account_keys.begin (), it));

	// Recover the claim fields from the cemented frontier for the target account.
	auto ch = ledger.store.confirmation_height.get (transaction, account);
	if (!ch.has_value () || ch->height == 0)
	{
		return std::nullopt;
	}
	auto balance = ledger.cemented.block_balance (transaction, ch->frontier);
	if (!balance.has_value ())
	{
		return std::nullopt;
	}

	nano::state_proof proof;
	proof.account_claim = nano::state_proof_account_claim{ account, ch->frontier, balance.value (), ch->height };
	build_mmr_proof (leaves.account_leaves, target, proof.path, proof.peaks, proof.peak_index);
	proof.other_root = mmr_root (leaves.pending_leaves);
	proof.account_count = leaves.account_leaves.size ();
	proof.pending_count = leaves.pending_leaves.size ();
	auto accounts_root = mmr_root (leaves.account_leaves);
	proof.root = overall_root (accounts_root, proof.other_root, proof.account_count, proof.pending_count);
	return proof;
}

std::optional<nano::state_proof> nano::generate_pending_proof (nano::ledger const & ledger, nano::secure::transaction const & transaction, nano::pending_key const & key)
{
	auto leaves = collect (ledger, transaction);
	uint64_t target = 0;
	bool found = false;
	for (uint64_t i = 0; i < leaves.pending_keys.size (); ++i)
	{
		if (leaves.pending_keys[i].account == key.account && leaves.pending_keys[i].hash == key.hash)
		{
			target = i;
			found = true;
			break;
		}
	}
	if (!found)
	{
		return std::nullopt;
	}
	auto info = ledger.store.pending.get (transaction, key);
	if (!info.has_value ())
	{
		return std::nullopt;
	}

	nano::state_proof proof;
	proof.pending_claim = nano::state_proof_pending_claim{ key.account, key.hash, info->source, info->amount, info->epoch };
	build_mmr_proof (leaves.pending_leaves, target, proof.path, proof.peaks, proof.peak_index);
	proof.other_root = mmr_root (leaves.account_leaves);
	proof.account_count = leaves.account_leaves.size ();
	proof.pending_count = leaves.pending_leaves.size ();
	auto pending_root = mmr_root (leaves.pending_leaves);
	proof.root = overall_root (proof.other_root, pending_root, proof.account_count, proof.pending_count);
	return proof;
}

bool nano::verify_state_proof (nano::state_proof const & proof)
{
	// Exactly one claim must be present.
	if (proof.account_claim.has_value () == proof.pending_claim.has_value ())
	{
		return false;
	}
	bool const is_account = proof.account_claim.has_value ();

	// Recompute the leaf from the claim, binding the claim to the proof.
	nano::block_hash leaf;
	if (is_account)
	{
		auto const & c = proof.account_claim.value ();
		leaf = account_leaf (c.account, c.frontier, c.balance, c.height);
	}
	else
	{
		auto const & c = proof.pending_claim.value ();
		nano::pending_key key{ c.account, c.hash };
		nano::pending_info info{ c.source, c.amount, c.epoch };
		leaf = pending_leaf (key, info);
	}

	// Climb to the mountain peak.
	nano::block_hash node = leaf;
	for (auto const & step : proof.path)
	{
		node = step.sibling_on_right ? hash_internal_node (node, step.hash) : hash_internal_node (step.hash, node);
	}
	if (proof.peak_index >= proof.peaks.size () || !(proof.peaks[proof.peak_index] == node))
	{
		return false;
	}

	// Rebag the peaks exactly as mmr_root does.
	std::optional<nano::block_hash> accumulator;
	for (auto const & peak : proof.peaks)
	{
		accumulator = accumulator.has_value () ? hash_internal_node (peak, accumulator.value ()) : peak;
	}
	nano::block_hash const sub = accumulator.has_value () ? accumulator.value () : empty_root ();

	nano::block_hash const accounts_root = is_account ? sub : proof.other_root;
	nano::block_hash const pending_root = is_account ? proof.other_root : sub;
	return overall_root (accounts_root, pending_root, proof.account_count, proof.pending_count) == proof.root;
}

nano::state_checkpoint nano::capture_state_checkpoint (nano::ledger const & ledger, nano::secure::transaction const & transaction)
{
	auto commitment = nano::compute_state_commitment (ledger, transaction);
	nano::state_checkpoint checkpoint;
	checkpoint.cemented_height = ledger.cemented_count ();
	checkpoint.root = commitment.root;
	checkpoint.account_count = commitment.account_count;
	checkpoint.pending_count = commitment.pending_count;
	return checkpoint;
}

nano::capped_retention_plan nano::plan_capped_retention (nano::ledger const & ledger, nano::secure::transaction const & transaction, uint64_t history_window, uint64_t max_accounts_to_prove)
{
	nano::capped_retention_plan plan;
	plan.checkpoint = nano::capture_state_checkpoint (ledger, transaction);
	plan.history_window = history_window;
	plan.retained_pending = plan.checkpoint.pending_count;

	// Approximate on-disk cost of one cemented state block: the serialized block plus
	// its sideband. This is an estimate for reporting reclaimable storage, not an exact
	// per-record figure (index / page overhead is not modelled).
	uint64_t const approx_stored_block_bytes = nano::state_block::size + nano::block_sideband::size (nano::block_type::state);

	// Classify cemented blocks per account: keep the top `history_window` (frontier
	// included), drop the rest. Content below the window stays committed under the root.
	for (auto i = ledger.store.confirmation_height.begin (transaction), n = ledger.store.confirmation_height.end (transaction); i != n; ++i)
	{
		nano::confirmation_height_info const & info = i->second;
		uint64_t const height = info.height;
		if (height == 0)
		{
			continue;
		}
		uint64_t const kept = history_window == 0 ? height : std::min (height, history_window);
		plan.kept_blocks += kept;
		plan.droppable_blocks += height - kept;
	}
	plan.reclaimable_bytes = plan.droppable_blocks * approx_stored_block_bytes;

	// Safety gate: every retained account frontier must remain provable against the
	// checkpoint root. Prove up to `max_accounts_to_prove` accounts (0 = all).
	for (auto i = ledger.store.confirmation_height.begin (transaction), n = ledger.store.confirmation_height.end (transaction); i != n; ++i)
	{
		if (max_accounts_to_prove != 0 && plan.accounts_checked >= max_accounts_to_prove)
		{
			break;
		}
		nano::account const & account = i->first;
		if (i->second.height == 0)
		{
			continue;
		}
		auto proof = nano::generate_account_proof (ledger, transaction, account);
		++plan.accounts_checked;
		if (proof.has_value () && proof->root == plan.checkpoint.root && nano::verify_state_proof (proof.value ()))
		{
			++plan.accounts_proven;
		}
	}
	plan.all_proven = plan.accounts_proven == plan.accounts_checked;
	return plan;
}
