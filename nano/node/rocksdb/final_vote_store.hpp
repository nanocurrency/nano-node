#pragma once

#include <nano/secure/store.hpp>

namespace nano
{
class rocksdb_store;
namespace rocksdb
{
	class final_vote_store : public nano::final_vote_store
	{
	private:
		nano::rocksdb_store & store;

	public:
		explicit final_vote_store (nano::rocksdb_store & store);
		bool put (nano::write_transaction const & transaction_a, nano::qualified_root const & root_a, nano::block_hash const & hash_a) override;
		std::vector<nano::block_hash> get (nano::transaction const & transaction_a, nano::root const & root_a) override;
		void del (nano::write_transaction const & transaction_a, nano::root const & root_a) override;
		size_t count (nano::transaction const & transaction_a) const override;
		void clear (nano::write_transaction const & transaction_a, nano::root const & root_a) override;
		void clear (nano::write_transaction const & transaction_a) override;
		nano::store_iterator<nano::qualified_root, nano::block_hash> begin (nano::transaction const & transaction_a, nano::qualified_root const & root_a) const override;
		nano::store_iterator<nano::qualified_root, nano::block_hash> begin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<nano::qualified_root, nano::block_hash> end () const override;
		void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::qualified_root, nano::block_hash>, nano::store_iterator<nano::qualified_root, nano::block_hash>)> const & action_a) const override;
	};
}
}
