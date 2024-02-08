#pragma once

#include <atomic>
#include <random>

namespace nano
{
class id_dispenser
{
public:
	enum class mode
	{
		sequential,
		random,
	};

	// Using pointer type for prettier and more concise output in logs (hex)
	using id_t = void *;

public:
	explicit id_dispenser (mode mode = mode::random) :
		mode_m{ mode }
	{
	}

	id_t next_id ()
	{
		switch (mode_m)
		{
			case mode::sequential:
				return reinterpret_cast<id_t> (current_id_m.fetch_add (1));
			case mode::random:
				auto value = get_dist () (get_rng ());
				if (value < min_m)
				{
					value += min_m;
				}
				return reinterpret_cast<id_t> (value);
		}
		return 0;
	}

private:
	// Avoid IDs with leading 0s for nicer output in logs
	static constexpr uint64_t min_m{ 0x1000000000000000 };

	mode mode_m;
	std::atomic<uint64_t> current_id_m{ min_m };

	static std::mt19937 & get_rng ()
	{
		static thread_local std::mt19937 rng{ std::random_device{}() };
		return rng;
	}

	static std::uniform_int_distribution<uint64_t> & get_dist ()
	{
		static thread_local std::uniform_int_distribution<uint64_t> dist;
		return dist;
	}
};

inline id_dispenser & id_gen ()
{
	static id_dispenser id_gen;
	return id_gen;
}

using id_t = id_dispenser::id_t;

inline id_t next_id ()
{
	return id_gen ().next_id ();
}
}