#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <nano/secure/common.hpp>

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
private:
	std::unordered_map<decltype (std::declval<V> ().full_hash ()), std::weak_ptr<V>> cache;
	std::mutex cache_mutex;

public:

	class factory_stats
	{
	public:
		size_t size {0};
		size_t cache_hit{ 0 };
		size_t cache_miss{ 0 };
		size_t created{ 0 };
		size_t erased{ 0 };
	} stats;

	factory_stats get_stats ()
	{
		std::unique_lock<std::mutex> lock (cache_mutex);
		stats.size = cache.size ();
		return stats;
	}

	template <typename T = V, typename... Args>
	std::shared_ptr<T> make_or_get (Args &&... args)
	{
		std::unique_lock<std::mutex> lock (cache_mutex);
		auto deleter = [this](T * w) {
			{
				std::lock_guard<std::mutex> lock (cache_mutex);
				this->cache.erase (w->full_hash ());
				++this->stats.erased;
			}
			// Delete outside lock to avoid deadlock if the deleted object has the last
			// reference to another factory object.
			delete w;
		};

		// This is required to produce an object whose full_hash() is valid. This typically
		// deserializes a block or vote. If an instance already exists, index will return
		// that one and 'obj' will be deallocated once we go out of scope.
		auto obj (std::shared_ptr<T> (new T (std::forward<Args> (args)...), deleter));
		++stats.created;

		// The deleter locks, and it may get called when we go out of scope (if an equivalent object exists,
		// which means index() does not put obj into the cache)
		lock.unlock ();
		return index (obj);
	}

private:

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
			++stats.cache_miss;
		}
		else
		{
			++stats.cache_hit;
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
