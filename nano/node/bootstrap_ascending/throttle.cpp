#include <nano/lib/utility.hpp>
#include <nano/node/bootstrap_ascending/throttle.hpp>
#include <nano/secure/common.hpp>

nano::bootstrap_ascending::throttle::throttle (std::size_t size) :
	successes_m{ size }
{
	samples.insert (samples.end (), size, true);
	debug_assert (size > 0);
}

bool nano::bootstrap_ascending::throttle::throttled () const
{
	return successes_m == 0;
}

void nano::bootstrap_ascending::throttle::add (bool sample)
{
	debug_assert (!samples.empty ());
	pop ();
	samples.push_back (sample);
	if (sample)
	{
		++successes_m;
	}
	//dump ();
}

void nano::bootstrap_ascending::throttle::resize (size_t size)
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

size_t nano::bootstrap_ascending::throttle::size () const
{
	return samples.size ();
}

size_t nano::bootstrap_ascending::throttle::successes () const
{
	return successes_m;
}

void nano::bootstrap_ascending::throttle::pop ()
{
	//dump ();
	if (samples.front ())
	{
		--successes_m;
	}
	samples.pop_front ();
	//dump ();
}
