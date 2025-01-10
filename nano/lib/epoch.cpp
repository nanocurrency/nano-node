#include <celerix/lib/epoch.hpp>
#include <celerix/lib/utility.hpp>

std::underlying_type_t<celerix::epoch> celerix::normalized_epoch (celerix::epoch epoch_a)
{
	// Currently assumes that the epoch versions in the enum are sequential.
	auto start = std::underlying_type_t<celerix::epoch> (celerix::epoch::epoch_0);
	auto end = std::underlying_type_t<celerix::epoch> (epoch_a);
	debug_assert (end >= start);
	return end - start;
}
