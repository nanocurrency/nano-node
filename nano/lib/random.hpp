#pragma once

#include <random>

namespace nano
{
/**
 * Not safe for any crypto related code, use for non-crypto PRNG only.
 */
class random_generator final
{
public:
	/// Generate a random number in the range [min, max)
	auto random (auto min, auto max)
	{
		release_assert (min < max);
		std::uniform_int_distribution<decltype (min)> dist (min, max - 1);
		return dist (rng);
	}

	/// Generate a random number in the range [0, max)
	auto random (auto max)
	{
		return random (decltype (max){ 0 }, max);
	}

private:
	std::random_device device;
	std::default_random_engine rng{ device () };
};
}