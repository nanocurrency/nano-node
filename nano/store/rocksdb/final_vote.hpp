#pragma once

#include <nano/store/final.hpp>

namespace nano::store::rocksdb
{
class component;
}
namespace nano::store::rocksdb
{
class final_vote : public nano::store::final_vote
{
private:
	nano::store::rocksdb::component & store;

public:
	explicit final_vote (nano::store::rocksdb::component & store);
	bool put (store::write_transaction const & transaction_a, nano::qualified_root const & root_a, nano::block_hash const & hash_a) override;
	std::vector<nano::block_hash> get (store::transaction const & transaction_a, nano::root const & root_a) override;
	void del (store::write_transaction const & transaction_a, nano::root const & root_a) override;
	size_t count (store::transaction const & transaction_a) const override;
	void clear (store::write_transaction const & transaction_a, nano::root const & root_a) override;
	void clear (store::write_transaction const & transaction_a) override;
	iterator begin (store::transaction const & transaction_a, nano::qualified_root const & root_a) const override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end () const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;
};
} // namespace nano::store::rocksdb
