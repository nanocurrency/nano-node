#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/db_val_impl.hpp>
#include <celerix/store/reverse_iterator.hpp>
#include <celerix/store/typed_iterator.hpp>

#include <functional>

namespace celerix
{
class account_info;
class block_hash;
}
namespace celerix::store
{
/**
 * Manages account storage and iteration
 */
class account
{
public:
	using iterator = typed_iterator<celerix::account, celerix::account_info>;
	using reverse_iterator = store::reverse_iterator<iterator>;

public:
	virtual void put (write_transaction const & tx, celerix::account const &, celerix::account_info const &) = 0;
	virtual bool get (transaction const & tx, celerix::account const &, celerix::account_info &) = 0;
	std::optional<celerix::account_info> get (transaction const & tx, celerix::account const &);
	virtual void del (write_transaction const & tx, celerix::account const &) = 0;
	virtual bool exists (transaction const & tx, celerix::account const &) = 0;
	virtual size_t count (transaction const & tx) = 0;
	virtual iterator begin (transaction const & tx, celerix::account const &) const = 0;
	virtual iterator begin (transaction const & tx) const = 0;
	reverse_iterator rbegin (transaction const & tx) const;
	reverse_iterator rend (transaction const & tx) const;
	virtual iterator end (transaction const & tx) const = 0;
	virtual void for_each_par (std::function<void (read_transaction const & tx, iterator, iterator)> const &) const = 0;
};
} // namespace celerix::store
