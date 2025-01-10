#include <celerix/lib/epochs.hpp>
#include <celerix/lib/utility.hpp>

#include <algorithm>

celerix::link const & celerix::epochs::link (celerix::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).link;
}

bool celerix::epochs::is_epoch_link (celerix::link const & link_a) const
{
	return std::any_of (epochs_m.begin (), epochs_m.end (), [&link_a] (auto const & item_a) { return item_a.second.link == link_a; });
}

celerix::public_key const & celerix::epochs::signer (celerix::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).signer;
}

celerix::epoch celerix::epochs::epoch (celerix::link const & link_a) const
{
	auto existing (std::find_if (epochs_m.begin (), epochs_m.end (), [&link_a] (auto const & item_a) { return item_a.second.link == link_a; }));
	debug_assert (existing != epochs_m.end ());
	return existing->first;
}

void celerix::epochs::add (celerix::epoch epoch_a, celerix::public_key const & signer_a, celerix::link const & link_a)
{
	debug_assert (epochs_m.find (epoch_a) == epochs_m.end ());
	epochs_m[epoch_a] = { signer_a, link_a };
}

bool celerix::epochs::is_sequential (celerix::epoch epoch_a, celerix::epoch new_epoch_a)
{
	auto head_epoch = std::underlying_type_t<celerix::epoch> (epoch_a);
	bool is_valid_epoch (head_epoch >= std::underlying_type_t<celerix::epoch> (celerix::epoch::epoch_0));
	return is_valid_epoch && (std::underlying_type_t<celerix::epoch> (new_epoch_a) == (head_epoch + 1));
}
