#pragma once

#include <nano/store/frontier.hpp>
#include <nano/store/iterator.hpp>

namespace nano::store::rocksdb
{
class component;
}
namespace nano::store::rocksdb
{
class frontier : public nano::store::frontier
{
public:
	frontier (nano::store::rocksdb::component & store);
	void put (store::write_transaction const &, nano::block_hash const &, nano::account const &) override;
	nano::account get (store::transaction const &, nano::block_hash const &) const override;
	void del (store::write_transaction const &, nano::block_hash const &) override;
	store::iterator<nano::block_hash, nano::account> begin (store::transaction const &) const override;
	store::iterator<nano::block_hash, nano::account> begin (store::transaction const &, nano::block_hash const &) const override;
	store::iterator<nano::block_hash, nano::account> end () const override;
	void for_each_par (std::function<void (store::read_transaction const &, store::iterator<nano::block_hash, nano::account>, store::iterator<nano::block_hash, nano::account>)> const & action_a) const override;

private:
	nano::store::rocksdb::component & store;
};
} // namespace nano::store::rocksdb
