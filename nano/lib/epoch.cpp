#include <nano/lib/epoch.hpp>
#include <nano/lib/utility.hpp>

std::underlying_type_t<nano::epoch> nano::normalized_epoch (nano::epoch epoch_a)
{
	// Currently assumes that the epoch versions in the enum are sequential.
	auto start = std::underlying_type_t<nano::epoch> (nano::epoch::epoch_0);
	auto end = std::underlying_type_t<nano::epoch> (epoch_a);
	debug_assert (end >= start);
	return end - start;
}
