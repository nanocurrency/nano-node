#pragma once

#include <nano/store/peer.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
namespace lmdb
{
	class store;
	class peer_store : public nano::peer_store
	{
	private:
		nano::lmdb::store & store;

	public:
		explicit peer_store (nano::lmdb::store & store_a);
		void put (nano::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) override;
		void del (nano::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) override;
		bool exists (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) const override;
		size_t count (nano::transaction const & transaction_a) const override;
		void clear (nano::write_transaction const & transaction_a) override;
		nano::store_iterator<nano::endpoint_key, nano::no_value> begin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<nano::endpoint_key, nano::no_value> end () const override;

		/*
		 * Endpoints for peers
		 * nano::endpoint_key -> no_value
		*/
		MDB_dbi peers_handle{ 0 };
	};
}
}
