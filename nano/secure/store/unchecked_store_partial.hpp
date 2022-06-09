#pragma once

#include <nano/secure/store_partial.hpp>

#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

namespace mi = boost::multi_index;

namespace
{
template <typename T>
void parallel_traversal (std::function<void (T const &, T const &, bool const)> const & action);
}

namespace nano
{
template <typename Val, typename Derived_Store>
class store_partial;

template <typename Val, typename Derived_Store>
void release_assert_success (store_partial<Val, Derived_Store> const &, int const);

template <typename Val, typename Derived_Store>
class unchecked_store_partial : public unchecked_store
{
	class tag_random_access
	{
	};
	class tag_root
	{
	};

private:
	nano::store_partial<Val, Derived_Store> & store;

	friend void release_assert_success<Val, Derived_Store> (store_partial<Val, Derived_Store> const &, int const);
	static size_t constexpr mem_block_count_max = 256'000;

public:
	unchecked_store_partial (nano::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	void clear (nano::write_transaction const & transaction_a) override
	{
		nano::lock_guard<std::recursive_mutex> lock{ mutex };
		if (entries == nullptr)
		{
			auto status = store.drop (transaction_a, tables::unchecked);
			release_assert_success (store, status);
		}
		else
		{
			entries->clear ();
		}
	}

	void put (nano::write_transaction const & transaction_a, nano::unchecked_key const & key_a, nano::unchecked_info const & info_a) override
	{
		nano::lock_guard<std::recursive_mutex> lock{ mutex };
		// Check if we should be using memory but the memory container hasn't been constructed i.e. we're transitioning from disk to memory.
		if (entries == nullptr && use_memory ())
		{
			auto entries_new = std::make_unique<typename decltype (entries)::element_type> ();
			for_each (
			transaction_a, [&entries_new] (nano::unchecked_key const & key, nano::unchecked_info const & info) { entries_new->template get<tag_root> ().insert ({ key, info }); }, [&] () { return entries_new->size () < mem_block_count_max; });
			clear (transaction_a);
			entries = std::move (entries_new);
		}
		if (entries == nullptr)
		{
			nano::db_val<Val> info (info_a);
			auto status (store.put (transaction_a, tables::unchecked, key_a, info));
			release_assert_success (store, status);
		}
		else
		{
			entries->template get<tag_root> ().insert ({ key_a, info_a });
			while (entries->size () > mem_block_count_max)
			{
				entries->template get<tag_random_access> ().pop_front ();
			}
		}
	}

	void put (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a, std::shared_ptr<nano::block> const & block_a) override
	{
		nano::unchecked_key key{ hash_a, block_a->hash () };
		nano::unchecked_info info{ block_a, block_a->account (), nano::seconds_since_epoch (), nano::signature_verification::unknown };
		put (transaction_a, key, info);
	}

	bool exists (nano::transaction const & transaction_a, nano::unchecked_key const & unchecked_key_a) override
	{
		nano::lock_guard<std::recursive_mutex> lock{ mutex };
		if (entries == nullptr)
		{
			nano::db_val<Val> value;
			auto status (store.get (transaction_a, tables::unchecked, nano::db_val<Val> (unchecked_key_a), value));
			release_assert (store.success (status) || store.not_found (status));
			return (store.success (status));
		}
		else
		{
			return entries->template get<tag_root> ().count (unchecked_key_a) != 0;
		}
	}

	void del (nano::write_transaction const & transaction_a, nano::unchecked_key const & key_a) override
	{
		nano::lock_guard<std::recursive_mutex> lock{ mutex };
		if (entries == nullptr)
		{
			auto status (store.del (transaction_a, tables::unchecked, key_a));
			release_assert_success (store, status);
		}
		else
		{
			auto erased = entries->template get<tag_root> ().erase (key_a);
			release_assert (erased);
		}
	}

	void for_each (
	nano::transaction const & tx, std::function<void (nano::unchecked_key const &, nano::unchecked_info const &)> action, std::function<bool ()> predicate = [] () { return true; }) override
	{
		nano::lock_guard<std::recursive_mutex> lock{ mutex };
		if (entries == nullptr)
		{
			for (auto i = store.template make_iterator<nano::unchecked_key, nano::unchecked_info> (tx, tables::unchecked), n = nano::store_iterator<nano::unchecked_key, nano::unchecked_info> (nullptr); predicate () && i != n; ++i)
			{
				action (i->first, i->second);
			}
		}
		else
		{
			for (auto i = entries->begin (), n = entries->end (); predicate () && i != n; ++i)
			{
				action (i->key, i->info);
			}
		}
	}

	void for_each (
	nano::transaction const & tx, nano::hash_or_account const & dependency, std::function<void (nano::unchecked_key const &, nano::unchecked_info const &)> action, std::function<bool ()> predicate = [] () { return true; }) override
	{
		nano::lock_guard<std::recursive_mutex> lock{ mutex };
		if (entries == nullptr)
		{
			for (auto i = store.template make_iterator<nano::unchecked_key, nano::unchecked_info> (tx, tables::unchecked, nano::unchecked_key{ dependency, 0 }), n = nano::store_iterator<nano::unchecked_key, nano::unchecked_info> (nullptr); predicate () && i != n && i->first.key () == dependency; ++i)
			{
				action (i->first, i->second);
			}
		}
		else
		{
			for (auto i = entries->template get<tag_root> ().lower_bound (nano::unchecked_key{ dependency, 0 }), n = entries->template get<tag_root> ().end (); predicate () && i != n && i->key.key () == dependency; ++i)
			{
				action (i->key, i->info);
			}
		}
	}

	std::vector<nano::unchecked_info> get (nano::transaction const & tx, nano::block_hash const & hash) override
	{
		std::vector<nano::unchecked_info> result;
		for_each (tx, hash, [&result] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
			result.push_back (info);
		});
		return result;
	}

	size_t count (nano::transaction const & transaction_a) override
	{
		nano::lock_guard<std::recursive_mutex> lock{ mutex };
		if (entries == nullptr)
		{
			return store.count (transaction_a, tables::unchecked);
		}
		else
		{
			return entries->size ();
		}
	}

private:
	class entry
	{
	public:
		nano::unchecked_key key;
		nano::unchecked_info info;
	};
	std::recursive_mutex mutex;
	std::unique_ptr<mi::multi_index_container<entry,
	mi::indexed_by<
	mi::random_access<mi::tag<tag_random_access>>,
	mi::ordered_unique<mi::tag<tag_root>,
	mi::member<entry, nano::unchecked_key, &entry::key>>>>>
	entries;
};

}
