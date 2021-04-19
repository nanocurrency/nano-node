#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>

#include <iomanip>
#include <sstream>

namespace
{
template <typename U, std::enable_if_t<std::is_same<U, std::chrono::nanoseconds>::value> * = nullptr>
std::string typed_unit ()
{
	return "nanoseconds";
}

template <typename U, std::enable_if_t<std::is_same<U, std::chrono::microseconds>::value> * = nullptr>
std::string typed_unit ()
{
	return "microseconds";
}

template <typename U, std::enable_if_t<std::is_same<U, std::chrono::milliseconds>::value> * = nullptr>
std::string typed_unit ()
{
	return "milliseconds";
}

template <typename U, std::enable_if_t<std::is_same<U, std::chrono::seconds>::value> * = nullptr>
std::string typed_unit ()
{
	return "seconds";
}

template <typename U, std::enable_if_t<std::is_same<U, std::chrono::minutes>::value> * = nullptr>
std::string typed_unit ()
{
	return "minutes";
}

template <typename U, std::enable_if_t<std::is_same<U, std::chrono::hours>::value> * = nullptr>
std::string typed_unit ()
{
	return "hours";
}
}

template <typename UNIT, typename CLOCK>
nano::timer<UNIT, CLOCK>::timer (nano::timer_state state_a, std::string const & description_a) :
	desc (description_a)
{
	if (state_a == nano::timer_state::started)
	{
		start ();
	}
}

template <typename UNIT, typename CLOCK>
nano::timer<UNIT, CLOCK>::timer (std::string const & description_a) :
	desc (description_a)
{
}

template <typename UNIT, typename CLOCK>
nano::timer<UNIT, CLOCK>::timer (std::string const & description_a, nano::timer<UNIT, CLOCK> * parent_a) :
	parent (parent_a),
	desc (description_a)
{
}

template <typename UNIT, typename CLOCK>
nano::timer<UNIT, CLOCK> & nano::timer<UNIT, CLOCK>::set_minimum (UNIT minimum_a)
{
	minimum = minimum_a;
	return *this;
}

template <typename UNIT, typename CLOCK>
nano::timer<UNIT, CLOCK> & nano::timer<UNIT, CLOCK>::child (std::string const & description_a)
{
	children.emplace_back (description_a, this);
	return children.back ();
}

template <typename UNIT, typename CLOCK>
nano::timer<UNIT, CLOCK> & nano::timer<UNIT, CLOCK>::start_child (std::string const & description_a)
{
	auto & child_timer = child (description_a);
	child_timer.start ();
	return child_timer;
}

template <typename UNIT, typename CLOCK>
void nano::timer<UNIT, CLOCK>::start ()
{
	debug_assert (state == nano::timer_state::stopped);
	state = nano::timer_state::started;
	begin = CLOCK::now ();
}

template <typename UNIT, typename CLOCK>
UNIT nano::timer<UNIT, CLOCK>::restart ()
{
	auto current = ticks;
	state = nano::timer_state::started;
	begin = CLOCK::now ();
	ticks = UNIT::zero ();
	measurements = 0;
	return current;
}

template <typename UNIT, typename CLOCK>
UNIT nano::timer<UNIT, CLOCK>::pause ()
{
	++measurements;
	return stop ();
}

template <typename UNIT, typename CLOCK>
void nano::timer<UNIT, CLOCK>::update_ticks ()
{
	auto end = CLOCK::now ();
	ticks += std::chrono::duration_cast<UNIT> (end - begin);
}

template <typename UNIT, typename CLOCK>
UNIT nano::timer<UNIT, CLOCK>::stop ()
{
	debug_assert (state == nano::timer_state::started);
	state = nano::timer_state::stopped;

	update_ticks ();
	return ticks;
}

template <typename UNIT, typename CLOCK>
UNIT nano::timer<UNIT, CLOCK>::value ()
{
	if (state != nano::timer_state::stopped)
	{
		update_ticks ();
	}
	return ticks;
}

template <typename UNIT, typename CLOCK>
UNIT nano::timer<UNIT, CLOCK>::since_start () const
{
	auto end = CLOCK::now ();
	return std::chrono::duration_cast<UNIT> (end - begin);
}

template <typename UNIT, typename CLOCK>
bool nano::timer<UNIT, CLOCK>::after_deadline (UNIT duration_a)
{
	auto end = CLOCK::now ();
	return std::chrono::duration_cast<UNIT> (end - begin) > duration_a;
}

template <typename UNIT, typename CLOCK>
bool nano::timer<UNIT, CLOCK>::before_deadline (UNIT duration_a)
{
	auto end = CLOCK::now ();
	return std::chrono::duration_cast<UNIT> (end - begin) < duration_a;
}

template <typename UNIT, typename CLOCK>
void nano::timer<UNIT, CLOCK>::stop (std::ostream & stream_a)
{
	stop ();
	print (stream_a);
}

template <typename UNIT, typename CLOCK>
void nano::timer<UNIT, CLOCK>::stop (std::string & output_a)
{
	std::ostringstream stream;
	stop (stream);
	output_a = stream.str ();
}

template <typename UNIT, typename CLOCK>
void nano::timer<UNIT, CLOCK>::print (std::ostream & stream_a)
{
	if (ticks >= minimum)
	{
		// Print cumulative children first. Non-cumulative children prints directly.
		for (auto & child : children)
		{
			if (child.measurements > 0)
			{
				child.print (stream_a);
			}
		}

		auto current_parent = parent;
		while (current_parent)
		{
			stream_a << parent->desc << ".";
			current_parent = current_parent->parent;
		}

		stream_a << desc << ": " << ticks.count () << ' ' << unit ();
		if (measurements > 0)
		{
			stream_a << " (" << measurements << " measurements, " << std::setprecision (2) << std::fixed << static_cast<double> (ticks.count ()) / static_cast<double> (measurements) << ' ' << unit () << " avg)";
		}
		stream_a << std::endl;
	}
}

template <typename UNIT, typename CLOCK>
std::string nano::timer<UNIT, CLOCK>::unit () const
{
	return typed_unit<UNIT> ();
}

template <typename UNIT, typename CLOCK>
nano::timer_state nano::timer<UNIT, CLOCK>::current_state () const
{
	return state;
}

// Explicitly instantiate all realistically used timers
template class nano::timer<std::chrono::milliseconds, std::chrono::steady_clock>;
template class nano::timer<std::chrono::microseconds, std::chrono::steady_clock>;
template class nano::timer<std::chrono::nanoseconds, std::chrono::steady_clock>;
template class nano::timer<std::chrono::seconds, std::chrono::steady_clock>;
template class nano::timer<std::chrono::milliseconds, std::chrono::system_clock>;
template class nano::timer<std::chrono::microseconds, std::chrono::system_clock>;
template class nano::timer<std::chrono::nanoseconds, std::chrono::system_clock>;
template class nano::timer<std::chrono::seconds, std::chrono::system_clock>;
