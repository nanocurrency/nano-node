#pragma once

#include <deque>

namespace nano::bootstrap_ascending
{
// Class used to throttle the ascending bootstrapper once it reaches a steady state
// Tracks verify_result samples and signals throttling if no tracked samples have gotten results
class throttle
{
public:
	// Initialized with all true samples
	explicit throttle (std::size_t size);
	[[nodiscard]] bool throttled () const;
	void add (bool success);
	// Resizes the number of samples tracked
	// Drops the oldest samples if the size decreases
	// Adds false samples if the size increases
	void resize (std::size_t size);
	[[nodiscard]] std::size_t size () const;
	[[nodiscard]] std::size_t successes () const;

private:
	void pop ();
	// Bit set that tracks sample results. True when something was retrieved, false otherwise
	std::deque<bool> samples;
	std::size_t successes_m;
};
} // nano::boostrap_ascending
