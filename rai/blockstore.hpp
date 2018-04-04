#pragma once

#include <rai/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

namespace rai
{
/**
 * The value produced when iterating with \ref store_iterator
 */
class store_entry
{
public:
	store_entry ();
	void clear ();
	store_entry * operator-> ();
	rai::mdb_val first;
	rai::mdb_val second;
};

/**
 * Iterates the key/value pairs of a transaction
 */
class store_iterator
{
public:
	store_iterator (MDB_txn *, MDB_dbi);
	store_iterator (std::nullptr_t);
	store_iterator (MDB_txn *, MDB_dbi, MDB_val const &);
	store_iterator (rai::store_iterator &&);
	store_iterator (rai::store_iterator const &) = delete;
	~store_iterator ();
	rai::store_iterator & operator++ ();
	rai::store_iterator & operator= (rai::store_iterator &&);
	rai::store_iterator & operator= (rai::store_iterator const &) = delete;
	rai::store_entry & operator-> ();
	bool operator== (rai::store_iterator const &) const;
	bool operator!= (rai::store_iterator const &) const;
	MDB_cursor * cursor;
	rai::store_entry current;
};

class unchecked_block
{
public:
	unchecked_block (rai::block_hash, std::shared_ptr<rai::block>);
	std::shared_ptr<rai::block> block;
	rai::block_hash hash;
	rai::block_hash dependency;
};

class unchecked_by_hash
{
};

class unchecked_by_dependency
{
};

typedef boost::multi_index_container<
rai::unchecked_block,
boost::multi_index::indexed_by<
boost::multi_index::hashed_non_unique<boost::multi_index::tag<rai::unchecked_by_hash>, boost::multi_index::member<rai::unchecked_block, rai::block_hash, &rai::unchecked_block::hash>>,
boost::multi_index::hashed_non_unique<boost::multi_index::tag<rai::unchecked_by_dependency>, boost::multi_index::member<rai::unchecked_block, rai::block_hash, &rai::unchecked_block::dependency>>>>
unchecked_cache_t;

/**
 * Manages block storage and iteration
 */
class block_store
{
public:
	block_store (bool &, boost::filesystem::path const &, int lmdb_max_dbs = 128);

	MDB_dbi block_database (rai::block_type);
	void block_put_raw (MDB_txn *, MDB_dbi, rai::block_hash const &, MDB_val);
	void block_put (MDB_txn *, rai::block_hash const &, rai::block const &, rai::block_hash const & = rai::block_hash (0));
	MDB_val block_get_raw (MDB_txn *, rai::block_hash const &, rai::block_type &);
	rai::block_hash block_successor (MDB_txn *, rai::block_hash const &);
	void block_successor_clear (MDB_txn *, rai::block_hash const &);
	std::unique_ptr<rai::block> block_get (MDB_txn *, rai::block_hash const &);
	std::unique_ptr<rai::block> block_random (MDB_txn *);
	std::unique_ptr<rai::block> block_random (MDB_txn *, MDB_dbi);
	void block_del (MDB_txn *, rai::block_hash const &);
	bool block_exists (MDB_txn *, rai::block_hash const &);
	rai::block_counts block_count (MDB_txn *);

	void frontier_put (MDB_txn *, rai::block_hash const &, rai::account const &);
	rai::account frontier_get (MDB_txn *, rai::block_hash const &);
	void frontier_del (MDB_txn *, rai::block_hash const &);
	size_t frontier_count (MDB_txn *);

	void account_put (MDB_txn *, rai::account const &, rai::account_info const &);
	bool account_get (MDB_txn *, rai::account const &, rai::account_info &);
	void account_del (MDB_txn *, rai::account const &);
	bool account_exists (MDB_txn *, rai::account const &);
	rai::store_iterator latest_begin (MDB_txn *, rai::account const &);
	rai::store_iterator latest_begin (MDB_txn *);
	rai::store_iterator latest_end ();

	void pending_put (MDB_txn *, rai::pending_key const &, rai::pending_info const &);
	void pending_del (MDB_txn *, rai::pending_key const &);
	bool pending_get (MDB_txn *, rai::pending_key const &, rai::pending_info &);
	bool pending_exists (MDB_txn *, rai::pending_key const &);
	rai::store_iterator pending_begin (MDB_txn *, rai::pending_key const &);
	rai::store_iterator pending_begin (MDB_txn *);
	rai::store_iterator pending_end ();

	void block_info_put (MDB_txn *, rai::block_hash const &, rai::block_info const &);
	void block_info_del (MDB_txn *, rai::block_hash const &);
	bool block_info_get (MDB_txn *, rai::block_hash const &, rai::block_info &);
	bool block_info_exists (MDB_txn *, rai::block_hash const &);
	rai::store_iterator block_info_begin (MDB_txn *, rai::block_hash const &);
	rai::store_iterator block_info_begin (MDB_txn *);
	rai::store_iterator block_info_end ();
	rai::uint128_t block_balance (MDB_txn *, rai::block_hash const &);
	static size_t const block_info_max = 32;

	rai::uint128_t representation_get (MDB_txn *, rai::account const &);
	void representation_put (MDB_txn *, rai::account const &, rai::uint128_t const &);
	void representation_add (MDB_txn *, rai::account const &, rai::uint128_t const &);
	rai::store_iterator representation_begin (MDB_txn *);
	rai::store_iterator representation_end ();

	void unchecked_clear (MDB_txn *);
	void unchecked_put (MDB_txn *, rai::block_hash const &, std::shared_ptr<rai::block> const &);
	std::vector<std::shared_ptr<rai::block>> unchecked_get (MDB_txn *, rai::block_hash const &);
	void unchecked_del (MDB_txn *, rai::block_hash const &, rai::block const &);
	rai::store_iterator unchecked_begin (MDB_txn *);
	rai::store_iterator unchecked_begin (MDB_txn *, rai::block_hash const &);
	rai::store_iterator unchecked_end ();
	size_t unchecked_count (MDB_txn *);
	unchecked_cache_t unchecked_cache;

	void unsynced_put (MDB_txn *, rai::block_hash const &);
	void unsynced_del (MDB_txn *, rai::block_hash const &);
	bool unsynced_exists (MDB_txn *, rai::block_hash const &);
	rai::store_iterator unsynced_begin (MDB_txn *, rai::block_hash const &);
	rai::store_iterator unsynced_begin (MDB_txn *);
	rai::store_iterator unsynced_end ();

	void checksum_put (MDB_txn *, uint64_t, uint8_t, rai::checksum const &);
	bool checksum_get (MDB_txn *, uint64_t, uint8_t, rai::checksum &);
	void checksum_del (MDB_txn *, uint64_t, uint8_t);

	// Return latest vote for an account from store
	std::shared_ptr<rai::vote> vote_get (MDB_txn *, rai::account const &);
	// Populate vote with the next sequence number
	std::shared_ptr<rai::vote> vote_generate (MDB_txn *, rai::account const &, rai::raw_key const &, std::shared_ptr<rai::block>);
	// Return either vote or the stored vote with a higher sequence number
	std::shared_ptr<rai::vote> vote_max (MDB_txn *, std::shared_ptr<rai::vote>);
	// Return latest vote for an account considering the vote cache
	std::shared_ptr<rai::vote> vote_current (MDB_txn *, rai::account const &);
	void flush (MDB_txn *);
	rai::store_iterator vote_begin (MDB_txn *);
	rai::store_iterator vote_end ();
	std::mutex cache_mutex;
	std::unordered_map<rai::account, std::shared_ptr<rai::vote>> vote_cache;

	void version_put (MDB_txn *, int);
	int version_get (MDB_txn *);
	void do_upgrades (MDB_txn *);
	void upgrade_v1_to_v2 (MDB_txn *);
	void upgrade_v2_to_v3 (MDB_txn *);
	void upgrade_v3_to_v4 (MDB_txn *);
	void upgrade_v4_to_v5 (MDB_txn *);
	void upgrade_v5_to_v6 (MDB_txn *);
	void upgrade_v6_to_v7 (MDB_txn *);
	void upgrade_v7_to_v8 (MDB_txn *);
	void upgrade_v8_to_v9 (MDB_txn *);
	void upgrade_v9_to_v10 (MDB_txn *);

	void clear (MDB_dbi);

	rai::mdb_env environment;
	// block_hash -> account                                        // Maps head blocks to owning account
	MDB_dbi frontiers;
	// account -> block_hash, representative, balance, timestamp    // Account to head block, representative, balance, last_change
	MDB_dbi accounts;
	// block_hash -> send_block
	MDB_dbi send_blocks;
	// block_hash -> receive_block
	MDB_dbi receive_blocks;
	// block_hash -> open_block
	MDB_dbi open_blocks;
	// block_hash -> change_block
	MDB_dbi change_blocks;
	// block_hash -> state_block
	MDB_dbi state_blocks;
	// block_hash -> sender, amount, destination                    // Pending blocks to sender account, amount, destination account
	MDB_dbi pending;
	// block_hash -> account, balance                               // Blocks info
	MDB_dbi blocks_info;
	// account -> weight                                            // Representation
	MDB_dbi representation;
	// block_hash -> block                                          // Unchecked bootstrap blocks
	MDB_dbi unchecked;
	// block_hash ->                                                // Blocks that haven't been broadcast
	MDB_dbi unsynced;
	// (uint56_t, uint8_t) -> block_hash                            // Mapping of region to checksum
	MDB_dbi checksum;
	// account -> uint64_t											// Highest vote observed for account
	MDB_dbi vote;
	// uint256_union -> ?											// Meta information about block store
	MDB_dbi meta;
};
}
