#pragma once

#include <boost/circular_buffer.hpp>

namespace nano::bootstrap_ascending
{
// Class used to throttle the ascending bootstrapper once it reaches a steady state
// Tracks verify_result samples and signals throttling if no tracked samples have gotten results
class throttle
{
public:
	// Initialized with all true samples
	explicit throttle (std::size_t size);
	bool throttled () const;
	void add (bool success);

private:
	// Rolling count of true samples in the sample buffer
	std::size_t successes;
	// Circular buffer that tracks sample results. True when something was retrieved, false otherwise
	boost::circular_buffer<bool> samples;
};
} // nano::boostrap_ascending
