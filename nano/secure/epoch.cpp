#include <nano/secure/epoch.hpp>

nano::uint256_union const & nano::epochs::link (nano::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).link;
}

bool nano::epochs::is_epoch_link (nano::uint256_union const & link_a) const
{
	return std::any_of (epochs_m.begin (), epochs_m.end (), [&link_a](auto const & item_a) { return item_a.second.link == link_a; });
}

nano::public_key const & nano::epochs::signer (nano::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).signer;
}

nano::epoch nano::epochs::epoch (nano::uint256_union const & link_a) const
{
	auto existing (std::find_if (epochs_m.begin (), epochs_m.end (), [&link_a](auto const & item_a) { return item_a.second.link == link_a; }));
	assert (existing != epochs_m.end ());
	return existing->first;
}

void nano::epochs::add (nano::epoch epoch_a, nano::public_key const & signer_a, nano::uint256_union const & link_a)
{
	assert (epochs_m.find (epoch_a) == epochs_m.end ());
	epochs_m[epoch_a] = { signer_a, link_a };
}
