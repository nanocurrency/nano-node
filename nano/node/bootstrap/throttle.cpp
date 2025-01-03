#include <nano/lib/utility.hpp>
#include <nano/node/bootstrap/throttle.hpp>

nano::bootstrap::throttle::throttle (std::size_t size) :
	successes_m{ size }
{
	samples.insert (samples.end (), size, true);
	debug_assert (size > 0);
}

bool nano::bootstrap::throttle::throttled () const
{
	return successes_m == 0;
}

void nano::bootstrap::throttle::add (bool sample)
{
	debug_assert (!samples.empty ());
	pop ();
	samples.push_back (sample);
	if (sample)
	{
		++successes_m;
	}
}

void nano::bootstrap::throttle::resize (std::size_t size)
{
	debug_assert (size > 0);
	while (size < samples.size ())
	{
		pop ();
	}
	while (size > samples.size ())
	{
		samples.push_back (false);
	}
}

std::size_t nano::bootstrap::throttle::size () const
{
	return samples.size ();
}

std::size_t nano::bootstrap::throttle::successes () const
{
	return successes_m;
}

void nano::bootstrap::throttle::pop ()
{
	if (samples.front ())
	{
		--successes_m;
	}
	samples.pop_front ();
}
