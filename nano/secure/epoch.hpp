#pragma once

#include <nano/lib/numbers.hpp>

#include <type_traits>
#include <unordered_map>

namespace nano
{
/**
 * Tag for which epoch an entry belongs to
 */
enum class epoch : uint8_t
{
	invalid = 0,
	unspecified = 1,
	epoch_begin = 2,
	epoch_0 = 2,
	epoch_1 = 3,
	epoch_2 = 4,
	max = epoch_2
};

/* This turns epoch_0 into 0 for instance */
std::underlying_type_t<nano::epoch> normalized_epoch (nano::epoch epoch_a);
}
namespace std
{
template <>
struct hash<::nano::epoch>
{
	std::size_t operator() (::nano::epoch const & epoch_a) const
	{
		std::hash<std::underlying_type_t<::nano::epoch>> hash;
		return hash (static_cast<std::underlying_type_t<::nano::epoch>> (epoch_a));
	}
};
}
namespace nano
{
class epoch_info
{
public:
	nano::public_key signer;
	nano::link link;
};
class epochs
{
public:
	bool is_epoch_link (nano::link const & link_a) const;
	nano::link const & link (nano::epoch epoch_a) const;
	nano::public_key const & signer (nano::epoch epoch_a) const;
	nano::epoch epoch (nano::link const & link_a) const;
	void add (nano::epoch epoch_a, nano::public_key const & signer_a, nano::link const & link_a);
	/** Checks that new_epoch is 1 version higher than epoch */
	static bool is_sequential (nano::epoch epoch_a, nano::epoch new_epoch_a);

private:
	std::unordered_map<nano::epoch, nano::epoch_info> epochs_m;
};
}
