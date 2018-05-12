#pragma once

#include <banano/common.hpp>

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
	ledger (rai::block_store &, rai::stat &, rai::block_hash const & = 0, rai::block_hash const & = 0);
	std::pair<rai::uint128_t, std::shared_ptr<rai::block>> winner (MDB_txn *, rai::votes const & votes_a);
	// Map of weight -> associated block, ordered greatest to least
	rai::tally_t tally (MDB_txn *, rai::votes const &);
	rai::account account (MDB_txn *, rai::block_hash const &);
	rai::uint128_t amount (MDB_txn *, rai::block_hash const &);
	rai::uint128_t balance (MDB_txn *, rai::block_hash const &);
	rai::uint128_t account_balance (MDB_txn *, rai::account const &);
	rai::uint128_t account_pending (MDB_txn *, rai::account const &);
	rai::uint128_t weight (MDB_txn *, rai::account const &);
	std::unique_ptr<rai::block> successor (MDB_txn *, rai::block_hash const &);
	std::unique_ptr<rai::block> forked_block (MDB_txn *, rai::block const &);
	rai::block_hash latest (MDB_txn *, rai::account const &);
	rai::block_hash latest_root (MDB_txn *, rai::account const &);
	rai::block_hash representative (MDB_txn *, rai::block_hash const &);
	rai::block_hash representative_calculated (MDB_txn *, rai::block_hash const &);
	bool block_exists (rai::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (rai::block_hash const &);
	bool is_send (MDB_txn *, rai::state_block const &);
	rai::block_hash block_destination (MDB_txn *, rai::block const &);
	rai::block_hash block_source (MDB_txn *, rai::block const &);
	rai::process_return process (MDB_txn *, rai::block const &);
	void rollback (MDB_txn *, rai::block_hash const &);
	void change_latest (MDB_txn *, rai::account const &, rai::block_hash const &, rai::account const &, rai::uint128_union const &, uint64_t, bool = false);
	void checksum_update (MDB_txn *, rai::block_hash const &);
	rai::checksum checksum (MDB_txn *, rai::account const &, rai::account const &);
	void dump_account_chain (rai::account const &);
	bool state_block_parsing_enabled (MDB_txn *);
	bool state_block_generation_enabled (MDB_txn *);
	static rai::uint128_t const unit;
	rai::block_store & store;
	rai::stat & stats;
	std::unordered_map<rai::account, rai::uint128_t> bootstrap_weights;
	uint64_t bootstrap_weight_max_blocks;
	std::atomic<bool> check_bootstrap_weights;
	rai::block_hash state_block_parse_canary;
	rai::block_hash state_block_generate_canary;
};
};
