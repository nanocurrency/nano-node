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
	store::iterator<nano::endpoint_key, nano::millis_t> begin (store::transaction const &) const override;
	store::iterator<nano::endpoint_key, nano::millis_t> end () const override;

	/*
	 * Endpoints for peers
	 * nano::endpoint_key -> no_value
	 */
	::lmdb::dbi peers_handle;
};
} // namespace nano::store::lmdb
