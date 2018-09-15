#pragma once

#include <galileo/secure/common.hpp>

struct MDB_txn;
namespace galileo
{
class block_store;
class stat;

class shared_ptr_block_hash
{
public:
	size_t operator() (std::shared_ptr<galileo::block> const &) const;
	bool operator() (std::shared_ptr<galileo::block> const &, std::shared_ptr<galileo::block> const &) const;
};
using tally_t = std::map<galileo::uint128_t, std::shared_ptr<galileo::block>, std::greater<galileo::uint128_t>>;
class ledger
{
public:
	ledger (galileo::block_store &, galileo::stat &, galileo::uint256_union const & = 1, galileo::account const & = 0);
	galileo::account account (galileo::transaction const &, galileo::block_hash const &);
	galileo::uint128_t amount (galileo::transaction const &, galileo::block_hash const &);
	galileo::uint128_t balance (galileo::transaction const &, galileo::block_hash const &);
	galileo::uint128_t account_balance (galileo::transaction const &, galileo::account const &);
	galileo::uint128_t account_pending (galileo::transaction const &, galileo::account const &);
	galileo::uint128_t weight (galileo::transaction const &, galileo::account const &);
	std::unique_ptr<galileo::block> successor (galileo::transaction const &, galileo::block_hash const &);
	std::unique_ptr<galileo::block> forked_block (galileo::transaction const &, galileo::block const &);
	galileo::block_hash latest (galileo::transaction const &, galileo::account const &);
	galileo::block_hash latest_root (galileo::transaction const &, galileo::account const &);
	galileo::block_hash representative (galileo::transaction const &, galileo::block_hash const &);
	galileo::block_hash representative_calculated (galileo::transaction const &, galileo::block_hash const &);
	bool block_exists (galileo::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (galileo::block_hash const &);
	bool is_send (galileo::transaction const &, galileo::state_block const &);
	galileo::block_hash block_destination (galileo::transaction const &, galileo::block const &);
	galileo::block_hash block_source (galileo::transaction const &, galileo::block const &);
	galileo::process_return process (galileo::transaction const &, galileo::block const &);
	void rollback (galileo::transaction const &, galileo::block_hash const &);
	void change_latest (galileo::transaction const &, galileo::account const &, galileo::block_hash const &, galileo::account const &, galileo::uint128_union const &, uint64_t, bool = false, galileo::epoch = galileo::epoch::epoch_0);
	void checksum_update (galileo::transaction const &, galileo::block_hash const &);
	galileo::checksum checksum (galileo::transaction const &, galileo::account const &, galileo::account const &);
	void dump_account_chain (galileo::account const &);
	bool could_fit (galileo::transaction const &, galileo::block const &);
	bool is_epoch_link (galileo::uint256_union const &);
	static galileo::uint128_t const unit;
	galileo::block_store & store;
	galileo::stat & stats;
	std::unordered_map<galileo::account, galileo::uint128_t> bootstrap_weights;
	uint64_t bootstrap_weight_max_blocks;
	std::atomic<bool> check_bootstrap_weights;
	galileo::uint256_union epoch_link;
	galileo::account epoch_signer;
};
};
