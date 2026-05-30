#pragma once

#include <nano/secure/extended/account_delegator_by_weight_key.hpp>
#include <nano/store/backend.hpp>
#include <nano/store/reverse_iterator.hpp>
#include <nano/store/reverse_iterator_templ.hpp>
#include <nano/store/typed_iterator.hpp>
#include <nano/store/typed_iterator_templ.hpp>

namespace nano::store::ledger
{
class account_delegator_by_weight_view
{
public:
	using iterator = nano::store::typed_iterator<nano::account_delegator_by_weight_key, std::nullptr_t>;
	using reverse_iterator = nano::store::reverse_iterator<iterator>;

	explicit account_delegator_by_weight_view (nano::store::backend &);

	void put (nano::store::write_transaction const &, nano::account_delegator_by_weight_key const &);
	void del (nano::store::write_transaction const &, nano::account_delegator_by_weight_key const &);
	bool empty (nano::store::transaction const &) const;
	uint64_t count (nano::store::transaction const &) const;
	void clear ();

	iterator begin (nano::store::transaction const &, nano::account_delegator_by_weight_key const &) const;
	iterator begin (nano::store::transaction const &) const;
	iterator end (nano::store::transaction const &) const;
	iterator upper_bound (nano::store::transaction const &, nano::account const &) const;
	reverse_iterator rupper_bound (nano::store::transaction const &, nano::account const &) const;
	reverse_iterator rend (nano::store::transaction const &) const;

private:
	nano::store::backend & backend;
};
}
