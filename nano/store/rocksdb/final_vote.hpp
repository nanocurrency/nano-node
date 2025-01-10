#pragma once

#include <celerix/store/final_vote.hpp>

namespace celerix::store::rocksdb
{
class component;
}
namespace celerix::store::rocksdb
{
class final_vote : public celerix::store::final_vote
{
private:
	celerix::store::rocksdb::component & store;

public:
	explicit final_vote (celerix::store::rocksdb::component & store);
	bool put (store::write_transaction const & transaction_a, celerix::qualified_root const & root_a, celerix::block_hash const & hash_a) override;
	std::optional<celerix::block_hash> get (store::transaction const & transaction_a, celerix::qualified_root const & qualified_root_a) override;
	void del (store::write_transaction const & transaction_a, celerix::qualified_root const & root_a) override;
	size_t count (store::transaction const & transaction_a) const override;
	void clear (store::write_transaction const & transaction_a) override;
	iterator begin (store::transaction const & transaction_a, celerix::qualified_root const & root_a) const override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;
};
} // namespace celerix::store::rocksdb
