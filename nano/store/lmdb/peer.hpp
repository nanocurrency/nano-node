#pragma once

#include <nano/store/peer.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::store::lmdb
{
class peer : public nano::store::peer
{
private:
	nano::store::lmdb::component & store;

public:
	explicit peer (nano::store::lmdb::component & store_a);
	void put (store::write_transaction const &, nano::endpoint_key const & endpoint, nano::millis_t timestamp) override;
	nano::millis_t get (store::transaction const &, nano::endpoint_key const & endpoint) const override;
	void del (store::write_transaction const &, nano::endpoint_key const & endpoint) override;
	bool exists (store::transaction const &, nano::endpoint_key const & endpoint) const override;
	size_t count (store::transaction const &) const override;
	void clear (store::write_transaction const &) override;
	iterator begin (store::transaction const &) const override;
	iterator end (store::transaction const & transaction_a) const override;

	/*
	 * Endpoints for peers
	 * nano::endpoint_key -> no_value
	 */
	MDB_dbi peers_handle{ 0 };
};
} // namespace nano::store::lmdb
