#pragma once

#include <nano/lib/interval.hpp>
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

		std::lock_guard guard{ mutex };

		if (cleanup_interval.elapsed ())
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
		std::lock_guard guard{ mutex };
		return values.size ();
	}

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const
	{
		std::lock_guard guard{ mutex };

		auto composite = std::make_unique<container_info_composite> (name);
		composite->add_component (std::make_unique<container_info_leaf> (container_info{ "cache", values.size (), sizeof (Value) }));
		return composite;
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
	mutable std::mutex mutex{};
	std::unordered_map<Key, std::weak_ptr<Value>> values{};
	nano::interval cleanup_interval{ cleanup_cutoff };
};
}