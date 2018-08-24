#pragma once

#include <boost/filesystem.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

#include <banano/lib/numbers.hpp>
#include <banano/secure/common.hpp>

namespace rai
{
/**
 * RAII wrapper for MDB_env
 */
class mdb_env
{
public:
	mdb_env (bool &, boost::filesystem::path const &, int max_dbs = 128);
	~mdb_env ();
	operator MDB_env * () const;
	MDB_env * environment;
};

/**
 * Encapsulates MDB_val and provides uint256_union conversion of the data.
 */
class mdb_val
{
public:
	enum class no_value
	{
		dummy
	};
	mdb_val (rai::epoch = rai::epoch::unspecified);
	mdb_val (rai::account_info const &);
	mdb_val (rai::block_info const &);
	mdb_val (MDB_val const &, rai::epoch = rai::epoch::unspecified);
	mdb_val (rai::pending_info const &);
	mdb_val (rai::pending_key const &);
	mdb_val (size_t, void *);
	mdb_val (rai::uint128_union const &);
	mdb_val (rai::uint256_union const &);
	mdb_val (std::shared_ptr<rai::block> const &);
	mdb_val (std::shared_ptr<rai::vote> const &);
	void * data () const;
	size_t size () const;
	explicit operator rai::account_info () const;
	explicit operator rai::block_info () const;
	explicit operator rai::pending_info () const;
	explicit operator rai::pending_key () const;
	explicit operator rai::uint128_union () const;
	explicit operator rai::uint256_union () const;
	explicit operator std::array<char, 64> () const;
	explicit operator no_value () const;
	explicit operator std::shared_ptr<rai::block> () const;
	explicit operator std::shared_ptr<rai::send_block> () const;
	explicit operator std::shared_ptr<rai::receive_block> () const;
	explicit operator std::shared_ptr<rai::open_block> () const;
	explicit operator std::shared_ptr<rai::change_block> () const;
	explicit operator std::shared_ptr<rai::state_block> () const;
	explicit operator std::shared_ptr<rai::vote> () const;
	explicit operator uint64_t () const;
	operator MDB_val * () const;
	operator MDB_val const & () const;
	MDB_val value;
	std::shared_ptr<std::vector<uint8_t>> buffer;
	rai::epoch epoch;
};

/**
 * RAII wrapper of MDB_txn where the constructor starts the transaction
 * and the destructor commits it.
 */
class transaction
{
public:
	transaction (rai::mdb_env &, MDB_txn *, bool);
	~transaction ();
	operator MDB_txn * () const;
	MDB_txn * handle;
	rai::mdb_env & environment;
};
class block_store;
/**
 * Determine the balance as of this block
 */
class balance_visitor : public rai::block_visitor
{
public:
	balance_visitor (MDB_txn *, rai::block_store &);
	virtual ~balance_visitor () = default;
	void compute (rai::block_hash const &);
	void send_block (rai::send_block const &) override;
	void receive_block (rai::receive_block const &) override;
	void open_block (rai::open_block const &) override;
	void change_block (rai::change_block const &) override;
	void state_block (rai::state_block const &) override;
	MDB_txn * transaction;
	rai::block_store & store;
	rai::block_hash current_balance;
	rai::block_hash current_amount;
	rai::uint128_t balance;
};

/**
 * Determine the amount delta resultant from this block
 */
class amount_visitor : public rai::block_visitor
{
public:
	amount_visitor (MDB_txn *, rai::block_store &);
	virtual ~amount_visitor () = default;
	void compute (rai::block_hash const &);
	void send_block (rai::send_block const &) override;
	void receive_block (rai::receive_block const &) override;
	void open_block (rai::open_block const &) override;
	void change_block (rai::change_block const &) override;
	void state_block (rai::state_block const &) override;
	void from_send (rai::block_hash const &);
	MDB_txn * transaction;
	rai::block_store & store;
	rai::block_hash current_amount;
	rai::block_hash current_balance;
	rai::uint128_t amount;
};

/**
 * Determine the representative for this block
 */
class representative_visitor : public rai::block_visitor
{
public:
	representative_visitor (MDB_txn * transaction_a, rai::block_store & store_a);
	virtual ~representative_visitor () = default;
	void compute (rai::block_hash const & hash_a);
	void send_block (rai::send_block const & block_a) override;
	void receive_block (rai::receive_block const & block_a) override;
	void open_block (rai::open_block const & block_a) override;
	void change_block (rai::change_block const & block_a) override;
	void state_block (rai::state_block const & block_a) override;
	MDB_txn * transaction;
	rai::block_store & store;
	rai::block_hash current;
	rai::block_hash result;
};
}
