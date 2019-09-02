#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/rep_weights.hpp>
#include <nano/secure/common.hpp>

namespace nano
{
class block_store;
class stat;

class shared_ptr_block_hash final
{
public:
	size_t operator() (std::shared_ptr<nano::block> const &) const;
	bool operator() (std::shared_ptr<nano::block> const &, std::shared_ptr<nano::block> const &) const;
};
using tally_t = std::map<nano::uint128_t, std::shared_ptr<nano::block>, std::greater<nano::uint128_t>>;
class ledger final
{
public:
	ledger (nano::block_store &, nano::stat &, bool = true, bool = true);
	nano::account account (nano::transaction const &, nano::block_hash const &) const;
	nano::uint128_t amount (nano::transaction const &, nano::block_hash const &);
	nano::uint128_t balance (nano::transaction const &, nano::block_hash const &) const;
	nano::uint128_t account_balance (nano::transaction const &, nano::account const &);
	nano::uint128_t account_pending (nano::transaction const &, nano::account const &);
	nano::uint128_t weight (nano::transaction const &, nano::account const &);
	std::shared_ptr<nano::block> successor (nano::transaction const &, nano::qualified_root const &);
	std::shared_ptr<nano::block> forked_block (nano::transaction const &, nano::block const &);
	bool block_confirmed (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const;
	bool block_not_confirmed_or_not_exists (nano::block const & block_a) const;
	nano::block_hash latest (nano::transaction const &, nano::account const &);
	nano::block_hash latest_root (nano::transaction const &, nano::account const &);
	nano::block_hash representative (nano::transaction const &, nano::block_hash const &);
	nano::block_hash representative_calculated (nano::transaction const &, nano::block_hash const &);
	bool block_exists (nano::block_hash const &);
	bool block_exists (nano::block_type, nano::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (nano::block_hash const &);
	bool is_send (nano::transaction const &, nano::state_block const &) const;
	nano::block_hash block_destination (nano::transaction const &, nano::block const &);
	nano::block_hash block_source (nano::transaction const &, nano::block const &);
	nano::process_return process (nano::write_transaction const &, nano::block const &, nano::signature_verification = nano::signature_verification::unknown);
	bool rollback (nano::write_transaction const &, nano::block_hash const &, std::vector<std::shared_ptr<nano::block>> &);
	bool rollback (nano::write_transaction const &, nano::block_hash const &);
	void change_latest (nano::write_transaction const &, nano::account const &, nano::block_hash const &, nano::account const &, nano::uint128_union const &, uint64_t, nano::epoch = nano::epoch::epoch_0);
	void dump_account_chain (nano::account const &);
	bool could_fit (nano::transaction const &, nano::block const &);
	bool is_epoch_link (nano::uint256_union const &);
	nano::account signer (nano::uint256_union const &) const;
	nano::uint256_union link (nano::epoch) const;
	static nano::uint128_t const unit;
	nano::network_params network_params;
	nano::block_store & store;
	std::atomic<uint64_t> cemented_count{ 0 };
	nano::rep_weights rep_weights;
	nano::stat & stats;
	std::unordered_map<nano::account, nano::uint128_t> bootstrap_weights;
	std::atomic<size_t> bootstrap_weights_size{ 0 };
	uint64_t bootstrap_weight_max_blocks{ 1 };
	std::atomic<bool> check_bootstrap_weights;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (ledger & ledger, const std::string & name);
}
