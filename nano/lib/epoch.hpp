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
	/** Returns true if link matches one of the released epoch links.
	 *  WARNING: just because a legal block contains an epoch link, it does not mean it is an epoch block.
	 *  A legal block containing an epoch link can easily be constructed by sending to an address identical
	 *  to one of the epoch links.
	 *  Epoch blocks follow the following rules and a block must satisfy them all to be a true epoch block:
	 *    epoch blocks are always state blocks
	 *    epoch blocks never change the balance of an account
	 *    epoch blocks always have a link field that starts with the ascii bytes "epoch v1 block" or "epoch v2 block" (and possibly others in the future)
	 *    epoch blocks never change the representative
	 *    epoch blocks are not signed by the account key, they are signed either by genesis or by special epoch keys
	 */
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
