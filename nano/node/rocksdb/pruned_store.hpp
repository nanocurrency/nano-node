#pragma once

#include <nano/secure/store.hpp>

namespace nano
{
namespace rocksdb
{
	class store;
	class pruned_store : public nano::pruned_store
	{
	private:
		nano::rocksdb::store & store;

	public:
		explicit pruned_store (nano::rocksdb::store & store_a);
		void put (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a) override;
		void del (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a) override;
		bool exists (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
		nano::block_hash random (nano::transaction const & transaction_a) override;
		size_t count (nano::transaction const & transaction_a) const override;
		void clear (nano::write_transaction const & transaction_a) override;
		nano::store_iterator<nano::block_hash, std::nullptr_t> begin (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
		nano::store_iterator<nano::block_hash, std::nullptr_t> begin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<nano::block_hash, std::nullptr_t> end () const override;
		void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, std::nullptr_t>, nano::store_iterator<nano::block_hash, std::nullptr_t>)> const & action_a) const override;
	};
}
}
