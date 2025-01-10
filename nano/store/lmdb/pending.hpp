#pragma once

#include <celerix/store/pending.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace celerix::store::lmdb
{
class component;
}
namespace celerix::store::lmdb
{
class pending : public celerix::store::pending
{
private:
	celerix::store::lmdb::component & store;

public:
	explicit pending (celerix::store::lmdb::component & store_a);
	void put (store::write_transaction const & transaction_a, celerix::pending_key const & key_a, celerix::pending_info const & pending_info_a) override;
	void del (store::write_transaction const & transaction_a, celerix::pending_key const & key_a) override;
	std::optional<celerix::pending_info> get (store::transaction const & transaction_a, celerix::pending_key const & key_a) override;
	bool exists (store::transaction const & transaction_a, celerix::pending_key const & key_a) override;
	bool any (store::transaction const & transaction_a, celerix::account const & account_a) override;
	iterator begin (store::transaction const & transaction_a, celerix::pending_key const & key_a) const override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;

	/**
	 * Maps min_version 0 (destination account, pending block) to (source account, amount). (Removed)
	 * celerix::account, celerix::block_hash -> celerix::account, celerix::amount
	 */
	MDB_dbi pending_v0_handle{ 0 };

	/**
	 * Maps min_version 1 (destination account, pending block) to (source account, amount). (Removed)
	 * celerix::account, celerix::block_hash -> celerix::account, celerix::amount
	 */
	MDB_dbi pending_v1_handle{ 0 };

	/**
	 * Maps (destination account, pending block) to (source account, amount, version). (Removed)
	 * celerix::account, celerix::block_hash -> celerix::account, celerix::amount, celerix::epoch
	 */
	MDB_dbi pending_handle{ 0 };
};
} // namespace celerix::store::lmdb
