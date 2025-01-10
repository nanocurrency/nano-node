#pragma once

#include <celerix/store/account.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace celerix::store::lmdb
{
class component;
}
namespace celerix::store::lmdb
{
class account : public celerix::store::account
{
private:
	celerix::store::lmdb::component & store;

public:
	explicit account (celerix::store::lmdb::component & store_a);
	void put (store::write_transaction const & transaction, celerix::account const & account, celerix::account_info const & info) override;
	bool get (store::transaction const & transaction_a, celerix::account const & account_a, celerix::account_info & info_a) override;
	void del (store::write_transaction const & transaction_a, celerix::account const & account_a) override;
	bool exists (store::transaction const & transaction_a, celerix::account const & account_a) override;
	size_t count (store::transaction const & transaction_a) override;
	iterator begin (store::transaction const & transaction_a, celerix::account const & account_a) const override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;

	/**
	 * Maps account v1 to account information, head, rep, open, balance, timestamp and block count. (Removed)
	 * celerix::account -> celerix::block_hash, celerix::block_hash, celerix::block_hash, celerix::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v0_handle{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp and block count. (Removed)
	 * celerix::account -> celerix::block_hash, celerix::block_hash, celerix::block_hash, celerix::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v1_handle{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp, block count and epoch
	 * celerix::account -> celerix::block_hash, celerix::block_hash, celerix::block_hash, celerix::amount, uint64_t, uint64_t, celerix::epoch
	 */
	MDB_dbi accounts_handle{ 0 };

	/**
	 * Representative weights. (Removed)
	 * celerix::account -> celerix::uint128_t
	 */
	MDB_dbi representation_handle{ 0 };
};
} // amespace celerix::store::lmdb
