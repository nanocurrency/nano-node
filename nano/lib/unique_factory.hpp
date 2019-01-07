#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <nano/secure/common.hpp>
#include <unordered_map>

namespace nano
{
class vote;
class block;

/**
 * Thread safe flyweight factory for votes and blocks. Type V must have a full_hash() member.
 * This is basically a hash consing provider to save memory by disposing equivalent
 * objects.
 */
template <typename V>
class unique_factory
{
	friend class unique_factory<nano::vote>;

private:
	std::unordered_map<decltype (std::declval<V> ().full_hash ()), std::weak_ptr<V>> cache;
	std::mutex cache_mutex;
	unique_factory<nano::block> * block_uniquer{ nullptr };

public:
	unique_factory (unique_factory<nano::block> * block_uniquer_a = nullptr) :
	block_uniquer (block_uniquer_a)
	{
	}

	size_t size () const
	{
		std::unique_lock<std::mutex> lock (cache_mutex);
		return cache.size ();
	}

	template <typename T = V, typename... Args>
	std::shared_ptr<T> make_or_get (Args &&... args)
	{
		std::unique_lock<std::mutex> lock (cache_mutex);
		auto deleter = [this](T * w) {
			{
				std::lock_guard<std::mutex> lock (cache_mutex);
				this->cache.erase (w->full_hash ());
				++this->erased;
			}
			// Delete outside lock to avoid deadlock if the deleted object has the last
			// reference to another factory object.
			delete w;
		};
		// This is required to produce an object whose full_hash() is valid
		auto obj (std::shared_ptr<T> (new T (std::forward<Args> (args)...), deleter));
		++created;
		// The deleter locks, and it may get called when we go out of scope (if an equivalent object exists,
		// which means index() does not put obj into the cache)
		lock.unlock ();
		//return index<T> (obj);
		return std::dynamic_pointer_cast<T> (unique (obj));
	}

	unsigned cache_hit{ 0 };
	unsigned cache_miss{ 0 };
	unsigned created{ 0 };
	unsigned erased{ 0 };

private:
	template <typename U = V, std::enable_if_t<std::is_same<std::shared_ptr<U>, std::shared_ptr<nano::vote>>::value> * = nullptr>
	std::shared_ptr<nano::vote> unique (std::shared_ptr<nano::vote> obj)
	{
		if (block_uniquer && obj && !obj->blocks[0].which ())
		{
			obj->blocks[0] = block_uniquer->unique (boost::get<std::shared_ptr<nano::block>> (obj->blocks[0]));
		}
		return obj ? index (obj) : obj;
	}

	template <typename U = V, std::enable_if_t<std::is_same<std::shared_ptr<U>, std::shared_ptr<nano::block>>::value> * = nullptr>
	std::shared_ptr<nano::block> unique (std::shared_ptr<nano::block> obj)
	{
		return obj ? index (obj) : obj;
	}

	/**
	 * Called to enlist the fully constructed object in the cache. That is, \p obj#full_hash must return a valid value.
	 * @return If an object with the same full_hash is already indexed, return that. Otherwise \p obj is returned.
	 */
	template <typename T = V>
	std::shared_ptr<T> index (std::shared_ptr<T> obj)
	{
		std::lock_guard<std::mutex> lock (cache_mutex);
		auto key = obj->full_hash ();
		auto & weak_entry = cache[key];
		std::shared_ptr<T> sp = std::dynamic_pointer_cast<T> (weak_entry.lock ());
		if (!sp)
		{
			sp = obj;
			weak_entry = sp;
			++cache_miss;
		}
		else
		{
			++cache_hit;
		}
		return sp;
	}
};

template <typename T, typename... Args>
std::shared_ptr<T> make_or_get (nano::unique_factory<nano::block> * factory, Args &&... args)
{
	std::shared_ptr<T> val;
	if (factory)
	{
		val = factory->make_or_get<T> (std::forward<Args> (args)...);
	}
	else
	{
		val = std::make_shared<T> (std::forward<Args> (args)...);
	}
	return val;
}
template <typename T, typename... Args>
std::shared_ptr<T> make_or_get (nano::unique_factory<nano::vote> * factory, Args &&... args)
{
	return (factory) ? factory->make_or_get (std::forward<Args> (args)...) : std::make_shared<T> (std::forward<Args> (args)...);
}
}
