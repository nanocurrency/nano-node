#pragma once

#include <celerix/store/pruned.hpp>

namespace celerix::store::rocksdb
{
class component;
}
namespace celerix::store::rocksdb
{
class pruned : public celerix::store::pruned
{
private:
	celerix::store::rocksdb::component & store;

public:
	explicit pruned (celerix::store::rocksdb::component & store_a);
	void put (store::write_transaction const & transaction_a, celerix::block_hash const & hash_a) override;
	void del (store::write_transaction const & transaction_a, celerix::block_hash const & hash_a) override;
	bool exists (store::transaction const & transaction_a, celerix::block_hash const & hash_a) const override;
	celerix::block_hash random (store::transaction const & transaction_a) override;
	size_t count (store::transaction const & transaction_a) const override;
	void clear (store::write_transaction const & transaction_a) override;
	iterator begin (store::transaction const & transaction_a, celerix::block_hash const & hash_a) const override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;
};
} // namespace celerix::store::rocksdb
