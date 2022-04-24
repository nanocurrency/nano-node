#pragma once

#include <nano/secure/store.hpp>

namespace nano
{
namespace rocksdb
{
	class store;
	class reverse_link_store : public nano::reverse_link_store
	{
	private:
		nano::rocksdb::store & store;

	public:
		explicit reverse_link_store (nano::rocksdb::store & store);
		void put (nano::write_transaction const & transaction_a, nano::block_hash const & send_block_hash_a, nano::block_hash const & receive_block_hash_a) override;
		nano::block_hash get (nano::transaction const & transaction_a, nano::block_hash const & send_block_hash_a) const override;
		void del (nano::write_transaction const & transaction_a, nano::block_hash const & send_block_hash_a) override;
		bool exists (nano::transaction const & transaction_a, nano::block_hash const & send_block_hash_a) const override;
		size_t count (nano::transaction const & transaction_a) const override;
		void clear (nano::write_transaction const & transaction_a) override;
		nano::store_iterator<nano::block_hash, nano::block_hash> begin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<nano::block_hash, nano::block_hash> begin (nano::transaction const & transaction_a, nano::block_hash const & send_block_hash_a) const override;
		nano::store_iterator<nano::block_hash, nano::block_hash> end () const override;
		void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, nano::block_hash>, nano::store_iterator<nano::block_hash, nano::block_hash>)> const & action_a) const override;
	};
}
}
