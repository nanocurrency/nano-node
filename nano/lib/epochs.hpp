#pragma once

#include <celerix/lib/epoch.hpp>
#include <celerix/lib/numbers.hpp>

#include <cstdint>
#include <unordered_map>

namespace celerix
{
class epoch_info
{
public:
	celerix::public_key signer;
	celerix::link link;
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
	bool is_epoch_link (celerix::link const & link_a) const;
	celerix::link const & link (celerix::epoch epoch_a) const;
	celerix::public_key const & signer (celerix::epoch epoch_a) const;
	celerix::epoch epoch (celerix::link const & link_a) const;
	void add (celerix::epoch epoch_a, celerix::public_key const & signer_a, celerix::link const & link_a);
	/** Checks that new_epoch is 1 version higher than epoch */
	static bool is_sequential (celerix::epoch epoch_a, celerix::epoch new_epoch_a);

private:
	std::unordered_map<celerix::epoch, celerix::epoch_info> epochs_m;
};
}
