#include <nano/lib/epoch.hpp>
#include <nano/lib/utility.hpp>

#include <algorithm>

nano::link const & nano::epochs::link (nano::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).link;
}

bool nano::epochs::is_epoch_link (nano::link const & link_a) const
{
	return std::any_of (epochs_m.begin (), epochs_m.end (), [&link_a] (auto const & item_a) { return item_a.second.link == link_a; });
}

nano::public_key const & nano::epochs::signer (nano::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).signer;
}

nano::epoch nano::epochs::epoch (nano::link const & link_a) const
{
	auto existing (std::find_if (epochs_m.begin (), epochs_m.end (), [&link_a] (auto const & item_a) { return item_a.second.link == link_a; }));
	debug_assert (existing != epochs_m.end ());
	return existing->first;
}

void nano::epochs::add (nano::epoch epoch_a, nano::public_key const & signer_a, nano::link const & link_a)
{
	debug_assert (epochs_m.find (epoch_a) == epochs_m.end ());
	epochs_m[epoch_a] = { signer_a, link_a };
}

bool nano::epochs::is_sequential (nano::epoch epoch_a, nano::epoch new_epoch_a)
{
	auto head_epoch = std::underlying_type_t<nano::epoch> (epoch_a);
	bool is_valid_epoch (head_epoch >= std::underlying_type_t<nano::epoch> (nano::epoch::epoch_0));
	return is_valid_epoch && (std::underlying_type_t<nano::epoch> (new_epoch_a) == (head_epoch + 1));
}

std::underlying_type_t<nano::epoch> nano::normalized_epoch (nano::epoch epoch_a)
{
	// Currently assumes that the epoch versions in the enum are sequential.
	auto start = std::underlying_type_t<nano::epoch> (nano::epoch::epoch_0);
	auto end = std::underlying_type_t<nano::epoch> (epoch_a);
	debug_assert (end >= start);
	return end - start;
}
