#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/backend.hpp>
#include <nano/store/block_w_sideband.hpp>
#include <nano/store/crawler.hpp>
#include <nano/store/typed_iterator.hpp>
#include <nano/store/typed_iterator_templ.hpp>

#include <functional>
#include <optional>

namespace nano::store::ledger
{
class successor_view;

class block_view
{
public:
	using iterator = nano::store::typed_iterator<nano::block_hash, block_w_sideband>;

public:
	block_view (nano::store::backend &, nano::store::ledger::successor_view &);

	void put (nano::store::write_transaction const &, nano::block_hash const &, nano::block const &);
	void raw_put (nano::store::write_transaction const &, std::vector<uint8_t> const & data, nano::block_hash const &);
	std::shared_ptr<nano::block> get (nano::store::transaction const &, nano::block_hash const &) const;
	void del (nano::store::write_transaction const &, nano::block_hash const &);
	bool exists (nano::store::transaction const &, nano::block_hash const &) const;
	uint64_t count (nano::store::transaction const &) const;
	iterator begin (nano::store::transaction const &, nano::block_hash const &) const;
	iterator begin (nano::store::transaction const &) const;
	iterator end (nano::store::transaction const &) const;
	void for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const;

	template <typename Transaction>
	auto crawl (Transaction & txn, nano::block_hash const & start = { 0 }) const -> nano::store::crawler<block_view, Transaction>
	{
		return nano::store::crawler<block_view, Transaction>{ *this, txn, start };
	}

private:
	void block_raw_get (nano::store::transaction const &, nano::block_hash const &, nano::store::db_val & value) const;

private:
	nano::store::backend & backend;
	nano::store::ledger::successor_view & successor_store;
};
}
