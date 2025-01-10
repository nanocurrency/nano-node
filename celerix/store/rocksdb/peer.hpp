#pragma once

#include <celerix/store/peer.hpp>

namespace celerix::store::rocksdb
{
class component;
}
namespace celerix::store::rocksdb
{
class peer : public celerix::store::peer
{
private:
	celerix::store::rocksdb::component & store;

public:
	explicit peer (celerix::store::rocksdb::component & store_a);
	void put (store::write_transaction const &, celerix::endpoint_key const & endpoint, celerix::millis_t timestamp) override;
	celerix::millis_t get (store::transaction const &, celerix::endpoint_key const & endpoint) const override;
	void del (store::write_transaction const &, celerix::endpoint_key const & endpoint) override;
	bool exists (store::transaction const &, celerix::endpoint_key const & endpoint) const override;
	size_t count (store::transaction const &) const override;
	void clear (store::write_transaction const &) override;
	iterator begin (store::transaction const &) const override;
	iterator end (store::transaction const & transaction_a) const override;
};
} // namespace celerix::store::rocksdb
