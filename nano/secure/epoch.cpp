#include <nano/secure/epoch.hpp>

nano::uint256_union nano::epochs::link (nano::epoch epoch_a) const
{
	assert (epoch_a == nano::epoch::epoch_1);
	return link_m;
}

bool nano::epochs::is_epoch_link (nano::uint256_union const & link_a)
{
	return link_a == link_m;
}

nano::public_key nano::epochs::signer (nano::uint256_union const & link_a) const
{
	assert (!signer_m.is_zero ());
	return signer_m;
}

void nano::epochs::add (nano::epoch epoch_a, nano::public_key const & signer_a, nano::uint256_union const & link_a)
{
	assert (epoch_a == nano::epoch::epoch_1);
	assert (link_m.is_zero ());
	assert (signer_m.is_zero ());
	signer_m = signer_a;
	link_m = link_a;
}
