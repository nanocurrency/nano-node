#include <nano/secure/epoch.hpp>

nano::link const & nano::epochs::link (nano::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).link;
}

bool nano::epochs::is_epoch_link (nano::link const & link_a) const
{
	return std::any_of (epochs_m.begin (), epochs_m.end (), [&link_a](auto const & item_a) { return item_a.second.link == link_a; });
}

nano::public_key const & nano::epochs::signer (nano::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).signer;
}

nano::epoch nano::epochs::epoch (nano::link const & link_a) const
{
	auto existing (std::find_if (epochs_m.begin (), epochs_m.end (), [&link_a](auto const & item_a) { return item_a.second.link == link_a; }));
	assert (existing != epochs_m.end ());
	return existing->first;
}

void nano::epochs::add (nano::epoch epoch_a, nano::public_key const & signer_a, nano::link const & link_a)
{
	assert (epochs_m.find (epoch_a) == epochs_m.end ());
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
	assert (end >= start);
	return end - start;
}

namespace
{
// Returns 0 is epochs are equal, -1 if lhs is less than rhs and 1 if rhs is greater than lhs
int compare (nano::epoch lhs_a, nano::epoch rhs_a)
{
	auto epoch_lhs = std::underlying_type_t<nano::epoch> (lhs_a);
	auto epoch_rhs = std::underlying_type_t<nano::epoch> (rhs_a);

	return (epoch_lhs < epoch_rhs) ? -1 : (epoch_lhs > epoch_rhs);
}
}

const char * nano::epoch_as_string (nano::epoch epoch)
{
	switch (epoch)
	{
		case nano::epoch::epoch_2:
			return "2";
		case nano::epoch::epoch_1:
			return "1";
		default:
			return "0";
	}
}

bool nano::is_epoch_greater (nano::epoch lhs_a, nano::epoch rhs_a)
{
	return compare (lhs_a, rhs_a) > 0;
}

std::ostream& operator<<(std::ostream& os, nano::epoch const& epoch)
{
	return os << epoch_as_string (epoch);
}

std::istream & operator>> (std::istream & istream, nano::epoch & epoch_a)
{
	std::underlying_type_t<nano::epoch> epoch;
	istream >> epoch;

	epoch_a = (epoch <= static_cast<std::underlying_type_t<nano::epoch>> (nano::epoch::max)) ? static_cast<nano::epoch> (epoch) : nano::epoch::invalid;
	return istream;
}
