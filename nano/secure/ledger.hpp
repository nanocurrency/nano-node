#pragma once

#include <rai/secure/common.hpp>

namespace rai
{
class block_store;
class stat;

class shared_ptr_block_hash
{
public:
	size_t operator() (std::shared_ptr<rai::block> const &) const;
	bool operator() (std::shared_ptr<rai::block> const &, std::shared_ptr<rai::block> const &) const;
};
using tally_t = std::map<rai::uint128_t, std::shared_ptr<rai::block>, std::greater<rai::uint128_t>>;
class ledger
{
public:
	ledger (rai::block_store &, rai::stat &, rai::uint256_union const & = 1, rai::account const & = 0);
	rai::account account (rai::transaction const &, rai::block_hash const &);
	rai::uint128_t amount (rai::transaction const &, rai::block_hash const &);
	rai::uint128_t balance (rai::transaction const &, rai::block_hash const &);
	rai::uint128_t account_balance (rai::transaction const &, rai::account const &);
	rai::uint128_t account_pending (rai::transaction const &, rai::account const &);
	rai::uint128_t weight (rai::transaction const &, rai::account const &);
	std::shared_ptr<rai::block> successor (rai::transaction const &, rai::block_hash const &);
	std::shared_ptr<rai::block> forked_block (rai::transaction const &, rai::block const &);
	rai::block_hash latest (rai::transaction const &, rai::account const &);
	rai::block_hash latest_root (rai::transaction const &, rai::account const &);
	rai::block_hash representative (rai::transaction const &, rai::block_hash const &);
	rai::block_hash representative_calculated (rai::transaction const &, rai::block_hash const &);
	bool block_exists (rai::block_hash const &);
	bool block_exists (rai::block_type, rai::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (rai::block_hash const &);
	bool is_send (rai::transaction const &, rai::state_block const &);
	rai::block_hash block_destination (rai::transaction const &, rai::block const &);
	rai::block_hash block_source (rai::transaction const &, rai::block const &);
	rai::process_return process (rai::transaction const &, rai::block const &, bool = false);
	void rollback (rai::transaction const &, rai::block_hash const &);
	void change_latest (rai::transaction const &, rai::account const &, rai::block_hash const &, rai::account const &, rai::uint128_union const &, uint64_t, bool = false, rai::epoch = rai::epoch::epoch_0);
	void checksum_update (rai::transaction const &, rai::block_hash const &);
	rai::checksum checksum (rai::transaction const &, rai::account const &, rai::account const &);
	void dump_account_chain (rai::account const &);
	bool could_fit (rai::transaction const &, rai::block const &);
	bool is_epoch_link (rai::uint256_union const &);
	static rai::uint128_t const unit;
	rai::block_store & store;
	rai::stat & stats;
	std::unordered_map<rai::account, rai::uint128_t> bootstrap_weights;
	uint64_t bootstrap_weight_max_blocks;
	std::atomic<bool> check_bootstrap_weights;
	rai::uint256_union epoch_link;
	rai::account epoch_signer;
};
};
