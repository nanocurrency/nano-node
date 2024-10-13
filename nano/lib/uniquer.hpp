#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/utility.hpp>

#include <memory>

namespace nano
{
template <typename Key, typename Value>
class uniquer final
{
public:
	using key_type = Key;
	using value_type = Value;

	std::shared_ptr<Value> unique (std::shared_ptr<Value> const & value)
	{
		if (value == nullptr)
		{
			return nullptr;
		}

		// Types used as value need to provide full_hash()
		Key hash = value->full_hash ();

		nano::lock_guard<nano::mutex> guard{ mutex };

		if (cleanup_interval.elapsed (cleanup_cutoff))
		{
			cleanup ();
		}

		auto & existing = values[hash];
		if (auto result = existing.lock ())
		{
			return result;
		}
		else
		{
			existing = value;
		}

		return value;
	}

	std::size_t size () const
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		return values.size ();
	}

	nano::container_info container_info () const
	{
		nano::lock_guard<nano::mutex> guard{ mutex };

		nano::container_info info;
		info.put ("cache", values);
		return info;
	}

	static std::chrono::milliseconds constexpr cleanup_cutoff{ 500 };

private:
	void cleanup ()
	{
		debug_assert (!mutex.try_lock ());

		std::erase_if (values, [] (auto const & item) {
			return item.second.expired ();
		});
	}

private:
	mutable nano::mutex mutex;
	std::unordered_map<Key, std::weak_ptr<Value>> values;
	nano::interval cleanup_interval;
};
}