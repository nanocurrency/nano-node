#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/secure/pending_info.hpp>
#include <nano/store/backend.hpp>
#include <nano/store/crawler.hpp>
#include <nano/store/typed_iterator.hpp>
#include <nano/store/typed_iterator_templ.hpp>

#include <functional>

namespace nano::store::ledger
{
class pending_view
{
public:
	using iterator = nano::store::typed_iterator<nano::pending_key, nano::pending_info>;

public:
	explicit pending_view (nano::store::backend &);

	void put (nano::store::write_transaction const &, nano::pending_key const &, nano::pending_info const &);
	void del (nano::store::write_transaction const &, nano::pending_key const &);
	std::optional<nano::pending_info> get (nano::store::transaction const &, nano::pending_key const &) const;
	bool exists (nano::store::transaction const &, nano::pending_key const &) const;
	bool any (nano::store::transaction const &, nano::account const &) const;
	uint64_t count (nano::store::transaction const &) const;
	iterator begin (nano::store::transaction const &, nano::pending_key const &) const;
	iterator begin (nano::store::transaction const &) const;
	iterator end (nano::store::transaction const &) const;
	void for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const;

	template <typename Transaction>
	auto crawl (Transaction & txn, nano::account const & start = { 0 }) const -> nano::store::crawler<pending_view, Transaction>
	{
		return nano::store::crawler<pending_view, Transaction>{ *this, txn, start };
	}

private:
	nano::store::backend & backend;
};
}

/**
 * Specialization for pending table which has a compound key (account + hash).
 * Groups entries by account, so group_key_type is nano::account.
 */
template <>
struct nano::store::crawler_traits<nano::pending_key, nano::pending_info>
{
	using group_key_type = nano::account;

	static nano::pending_key lower_bound_key (group_key_type const & account)
	{
		return nano::pending_key{ account, 0 };
	}

	static group_key_type group_key (nano::pending_key const & key)
	{
		return key.account;
	}

	static std::optional<group_key_type> next_group_key (group_key_type const & current)
	{
		return next_key (current);
	}
};