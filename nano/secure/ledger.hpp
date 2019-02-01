#pragma once

#include <nano/secure/common.hpp>

namespace nano
{
class block_store;
class stat;

class shared_ptr_block_hash
{
public:
	size_t operator() (std::shared_ptr<nano::block> const &) const;
	bool operator() (std::shared_ptr<nano::block> const &, std::shared_ptr<nano::block> const &) const;
};
using tally_t = std::map<nano::uint128_t, std::shared_ptr<nano::block>, std::greater<nano::uint128_t>>;
class ledger
{
public:
	ledger (nano::block_store &, nano::stat &, nano::uint256_union const & = 1, nano::account const & = 0);
	nano::account account (nano::transaction const &, nano::block_hash const &);
	nano::uint128_t amount (nano::transaction const &, nano::block_hash const &);
	nano::uint128_t balance (nano::transaction const &, nano::block_hash const &);
	nano::uint128_t account_balance (nano::transaction const &, nano::account const &);
	nano::uint128_t account_pending (nano::transaction const &, nano::account const &);
	nano::uint128_t weight (nano::transaction const &, nano::account const &);
	std::shared_ptr<nano::block> successor (nano::transaction const &, nano::uint512_union const &);
	std::shared_ptr<nano::block> forked_block (nano::transaction const &, nano::block const &);
	nano::block_hash latest (nano::transaction const &, nano::account const &);
	nano::block_hash latest_root (nano::transaction const &, nano::account const &);
	nano::block_hash representative (nano::transaction const &, nano::block_hash const &);
	nano::block_hash representative_calculated (nano::transaction const &, nano::block_hash const &);
	bool block_exists (nano::block_hash const &);
	bool block_exists (nano::block_type, nano::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (nano::block_hash const &);
	bool is_send (nano::transaction const &, nano::state_block const &);
	nano::block_hash block_destination (nano::transaction const &, nano::block const &);
	nano::block_hash block_source (nano::transaction const &, nano::block const &);
	nano::process_return process (nano::transaction const &, nano::block const &, nano::signature_verification = nano::signature_verification::unknown);
	void rollback (nano::transaction const &, nano::block_hash const &, std::vector<nano::block_hash> &);
	void rollback (nano::transaction const &, nano::block_hash const &);
	void change_latest (nano::transaction const &, nano::account const &, nano::block_hash const &, nano::account const &, nano::uint128_union const &, uint64_t, bool = false, nano::epoch = nano::epoch::epoch_0);
	void dump_account_chain (nano::account const &);
	bool could_fit (nano::transaction const &, nano::block const &);
	bool is_epoch_link (nano::uint256_union const &);
	static nano::uint128_t const unit;
	nano::block_store & store;
	nano::stat & stats;
	std::unordered_map<nano::account, nano::uint128_t> bootstrap_weights;
	uint64_t bootstrap_weight_max_blocks;
	std::atomic<bool> check_bootstrap_weights;
	nano::uint256_union epoch_link;
	nano::account epoch_signer;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (ledger & ledger, const std::string & name);
}
