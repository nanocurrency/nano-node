#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/secure/account_info.hpp>
#include <nano/store/backend.hpp>
#include <nano/store/crawler.hpp>
#include <nano/store/reverse_iterator.hpp>
#include <nano/store/reverse_iterator_templ.hpp>
#include <nano/store/typed_iterator.hpp>
#include <nano/store/typed_iterator_templ.hpp>

#include <functional>

namespace nano::store::ledger
{
class account_view
{
public:
	using iterator = store::typed_iterator<nano::account, nano::account_info>;
	using reverse_iterator = store::reverse_iterator<iterator>;
	using crawler = store::crawler<account_view>;

public:
	explicit account_view (nano::store::backend &);

	void put (nano::store::write_transaction const &, nano::account const &, nano::account_info const &);
	bool get (nano::store::transaction const &, nano::account const &, nano::account_info &) const;
	std::optional<nano::account_info> get (nano::store::transaction const &, nano::account const &) const;
	void del (nano::store::write_transaction const &, nano::account const &);
	bool exists (nano::store::transaction const &, nano::account const &) const;
	size_t count (nano::store::transaction const &) const;
	iterator begin (nano::store::transaction const &, nano::account const &) const;
	iterator begin (nano::store::transaction const &) const;
	reverse_iterator rbegin (nano::store::transaction const &) const;
	reverse_iterator rend (nano::store::transaction const &) const;
	iterator end (nano::store::transaction const &) const;
	crawler crawl (nano::store::transaction const &, nano::account const & start = { 0 }) const;
	void for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const;

private:
	nano::store::backend & backend;
};
}
