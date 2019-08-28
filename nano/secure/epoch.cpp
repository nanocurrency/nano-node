#include <nano/secure/epoch.hpp>

bool nano::epochs::is_epoch_link (nano::uint256_union const & link_a)
{
	return std::any_of (epochs_m.begin (), epochs_m.end (), [link_a] (nano::epoch_info const & item_a) { return item_a.link == link_a; });
}
