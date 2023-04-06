#include <nano/node/bootstrap_ascending/throttle.hpp>

nano::bootstrap_ascending::throttle::throttle (std::size_t count) :
	successes{ count },
	samples{ count, true }
{
}

bool nano::bootstrap_ascending::throttle::throttled () const
{
	return successes == 0;
}

void nano::bootstrap_ascending::throttle::add (bool sample)
{
	if (samples.front ())
	{
		--successes;
	}
	samples.push_back (sample);
	if (sample)
	{
		++successes;
	}
}