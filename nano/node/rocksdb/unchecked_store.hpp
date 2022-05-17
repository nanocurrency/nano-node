#pragma once

#include <nano/secure/store.hpp>

namespace nano
{
namespace rocksdb
{
	class store;
	class unchecked_store : public nano::unchecked_store
	{
	private:
		nano::rocksdb::store & store;

	public:
		unchecked_store (nano::rocksdb::store & store_a);

		void clear (nano::write_transaction const & transaction_a) override;
		void put (nano::write_transaction const & transaction_a, nano::hash_or_account const & dependency, nano::unchecked_info const & info_a) override;
		bool exists (nano::transaction const & transaction_a, nano::unchecked_key const & unchecked_key_a) override;
		void del (nano::write_transaction const & transaction_a, nano::unchecked_key const & key_a) override;
		nano::store_iterator<nano::unchecked_key, nano::unchecked_info> end () const override;
		nano::store_iterator<nano::unchecked_key, nano::unchecked_info> begin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<nano::unchecked_key, nano::unchecked_info> lower_bound (nano::transaction const & transaction_a, nano::unchecked_key const & key_a) const override;
		size_t count (nano::transaction const & transaction_a) override;
		void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::unchecked_key, nano::unchecked_info>, nano::store_iterator<nano::unchecked_key, nano::unchecked_info>)> const & action_a) const override;
        std::atomic<uint64_t> last_work;
	};
}
}
