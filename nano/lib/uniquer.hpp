#pragma once

#include <celerix/lib/interval.hpp>
#include <celerix/lib/locks.hpp>
#include <celerix/lib/utility.hpp>

#include <memory>

namespace celerix
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

		celerix::lock_guard<celerix::mutex> guard{ mutex };

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
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		return values.size ();
	}

	celerix::container_info container_info () const
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };

		celerix::container_info info;
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
	mutable celerix::mutex mutex;
	std::unordered_map<Key, std::weak_ptr<Value>> values;
	celerix::interval cleanup_interval;
};
}