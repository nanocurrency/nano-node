#include <nano/node/transport/reachout_index.hpp>
#include <nano/node/transport/transport.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <string_view>

using namespace std::chrono_literals;

namespace
{
using nano::transport::reachout_index;
using reachout_outcome = nano::transport::reachout::outcome;

// Addresses are given as the channels container sees them, IPv6 or IPv4 mapped into IPv6
nano::tcp_endpoint endpoint (std::string_view address, uint16_t port = 7075)
{
	return { boost::asio::ip::make_address (address), port };
}

// A hold of 10 s that doubles up to 40 s, so the expiries read in round numbers
reachout_index::limits test_limits ()
{
	reachout_index::limits limits;
	limits.cooldown = 10s;
	limits.cooldown_max = 40s;
	return limits;
}

// Injected time starts at the clock's epoch, every test moves forward from here
std::chrono::steady_clock::time_point const origin{};
}

/*
 * Tracking an endpoint hands out the handle of a pending reachout with its keys and hold, and the endpoint is held: another track hands out nothing, and another port on the same address is an endpoint of its own
 */
TEST (reachout_index, track)
{
	reachout_index index{ test_limits () };
	auto const peer = endpoint ("::ffff:10.0.0.1");

	// The reachout carries the endpoint, its per IP and per subnetwork keys and a hold of one cooldown from the injected start
	auto const reachout = index.track (peer, origin);
	ASSERT_NE (nullptr, reachout);
	ASSERT_EQ (peer, reachout->endpoint);
	ASSERT_EQ (nano::transport::ipv4_address_or_ipv6_subnet (peer.address ()), reachout->address);
	ASSERT_EQ (nano::transport::map_address_to_subnetwork (peer.address ()), reachout->subnetwork);
	ASSERT_EQ (origin, reachout->start);
	ASSERT_EQ (origin + 10s, reachout->expiry);
	ASSERT_EQ (0, reachout->failures);
	ASSERT_EQ (reachout_outcome::pending, reachout->result ());
	ASSERT_EQ (1, index.size ());

	// The endpoint is held
	ASSERT_EQ (nullptr, index.track (peer, origin + 9s));
	ASSERT_EQ (1, index.size ());

	// Another port on the same address is another endpoint
	auto const other = index.track (endpoint ("::ffff:10.0.0.1", 7076), origin);
	ASSERT_NE (nullptr, other);
	ASSERT_NE (reachout, other);
	ASSERT_EQ (2, index.size ());
}

/*
 * A reachout holds its endpoint for as long as its attempt runs, however far past its expiry that is: nothing replaces it until the attempt has ended
 */
TEST (reachout_index, pending_holds_past_expiry)
{
	reachout_index index{ test_limits () };
	auto const peer = endpoint ("::ffff:10.0.0.1");

	// The attempt is still running long after the hold of 10 s
	auto reachout = index.track (peer, origin);
	ASSERT_NE (nullptr, reachout);
	ASSERT_EQ (nullptr, index.track (peer, origin + 10s));
	ASSERT_EQ (nullptr, index.track (peer, origin + 30s));
	ASSERT_EQ (1, index.count_per_ip (peer.address (), origin + 30s));

	// Once it has ended, without a channel, the expired hold lets the next reachout in with the failure counted
	reachout.reset ();
	auto const next = index.track (peer, origin + 30s);
	ASSERT_NE (nullptr, next);
	ASSERT_EQ (1, next->failures);
}

/*
 * Letting go of a handle without an outcome reports a failure, once its last copy is gone, and a failed reachout still holds its endpoint until its expiry
 */
TEST (reachout_index, release_fails)
{
	reachout_index index{ test_limits () };
	auto const peer = endpoint ("::ffff:10.0.0.1");

	// A copy of the handle keeps the attempt running
	auto reachout = index.track (peer, origin);
	ASSERT_NE (nullptr, reachout);
	auto copy = reachout;
	reachout.reset ();
	ASSERT_EQ (reachout_outcome::pending, copy->result ());

	// The last copy going is the failure, which the handle of the next reachout shows in its failures
	copy.reset ();
	ASSERT_EQ (nullptr, index.track (peer, origin + 9s));
	auto const next = index.track (peer, origin + 10s);
	ASSERT_NE (nullptr, next);
	ASSERT_EQ (1, next->failures);
}

/*
 * Each reachout in a row that ends without a channel doubles the hold of the next one, up to the longest hold
 */
TEST (reachout_index, backoff)
{
	reachout_index index{ test_limits () };
	auto const peer = endpoint ("::ffff:10.0.0.1");

	// The first reachout holds for one cooldown, and fails as its handle is not kept
	ASSERT_EQ (origin + 10s, index.track (peer, origin)->expiry);

	// The next one holds twice as long
	auto second = index.track (peer, origin + 10s);
	ASSERT_NE (nullptr, second);
	ASSERT_EQ (1, second->failures);
	ASSERT_EQ (origin + 30s, second->expiry);
	second.reset ();
	ASSERT_EQ (nullptr, index.track (peer, origin + 29s));

	// And the one after that four times as long
	auto third = index.track (peer, origin + 30s);
	ASSERT_NE (nullptr, third);
	ASSERT_EQ (2, third->failures);
	ASSERT_EQ (origin + 70s, third->expiry);
	third.reset ();

	// The doubling stops at the longest hold while the failures keep counting
	auto const fourth = index.track (peer, origin + 70s);
	ASSERT_NE (nullptr, fourth);
	ASSERT_EQ (3, fourth->failures);
	ASSERT_EQ (origin + 110s, fourth->expiry);
}

/*
 * A reachout that ended in a channel holds its endpoint until its expiry all the same, and clears the failures so the next reachout holds for one cooldown again
 */
TEST (reachout_index, mark_connected)
{
	reachout_index index{ test_limits () };
	auto const peer = endpoint ("::ffff:10.0.0.1");

	// One reachout without a channel doubles the hold of the next
	ASSERT_NE (nullptr, index.track (peer, origin));
	auto reachout = index.track (peer, origin + 10s);
	ASSERT_NE (nullptr, reachout);
	ASSERT_EQ (1, reachout->failures);

	// The channel is the outcome, and letting go of the handle afterwards does not turn it into a failure
	reachout->mark_connected ();
	ASSERT_EQ (reachout_outcome::connected, reachout->result ());
	reachout.reset ();
	ASSERT_EQ (nullptr, index.track (peer, origin + 29s));

	// The next reachout starts from no failures
	auto const next = index.track (peer, origin + 30s);
	ASSERT_NE (nullptr, next);
	ASSERT_EQ (0, next->failures);
	ASSERT_EQ (origin + 40s, next->expiry);
}

/*
 * Dropping a reachout frees its endpoint at once and leaves its failures to the next reachout unchanged
 */
TEST (reachout_index, drop)
{
	reachout_index index{ test_limits () };
	auto const peer = endpoint ("::ffff:10.0.0.1");

	// One reachout without a channel doubles the hold of the next
	ASSERT_NE (nullptr, index.track (peer, origin));
	auto const second = index.track (peer, origin + 10s);
	ASSERT_NE (nullptr, second);
	ASSERT_EQ (1, second->failures);

	// Dropped, it stops holding before its expiry and no longer counts against the IP
	second->drop ();
	ASSERT_EQ (reachout_outcome::dropped, second->result ());
	ASSERT_EQ (0, index.count_per_ip (peer.address (), origin + 11s));

	// The next reachout follows at once with the same failures
	auto const third = index.track (peer, origin + 11s);
	ASSERT_NE (nullptr, third);
	ASSERT_EQ (1, third->failures);
	ASSERT_EQ (origin + 31s, third->expiry);
}

/*
 * The first outcome reported stands: a reachout reports through its own handle only, so a late or repeated report changes neither it nor the reachout that followed it
 */
TEST (reachout_index, first_outcome_stands)
{
	reachout_index index{ test_limits () };
	auto const peer = endpoint ("::ffff:10.0.0.1");

	// A drop after a channel changes nothing
	auto const connected = index.track (peer, origin);
	ASSERT_NE (nullptr, connected);
	connected->mark_connected ();
	connected->drop ();
	ASSERT_EQ (reachout_outcome::connected, connected->result ());
	ASSERT_EQ (nullptr, index.track (peer, origin + 9s));

	// Nor does a channel after a drop
	auto const dropped = index.track (peer, origin + 10s);
	ASSERT_NE (nullptr, dropped);
	dropped->drop ();
	dropped->mark_connected ();
	ASSERT_EQ (reachout_outcome::dropped, dropped->result ());

	// A report on a handle whose reachout has been replaced leaves the successor pending and holding
	auto const successor = index.track (peer, origin + 10s);
	ASSERT_NE (nullptr, successor);
	dropped->mark_connected ();
	connected->drop ();
	ASSERT_EQ (reachout_outcome::pending, successor->result ());
	ASSERT_EQ (nullptr, index.track (peer, origin + 11s));
	ASSERT_EQ (1, index.size ());
}

/*
 * Reachouts holding their endpoints count per IP, where an IPv4 address is one IP and an IPv6 /48 is one IP whatever the host part; an ended reachout stops counting at its expiry while it is still remembered
 */
TEST (reachout_index, count_per_ip)
{
	reachout_index index{ test_limits () };

	// Two ports of one IPv4 address count twice for it and not at all for its neighbour
	ASSERT_NE (nullptr, index.track (endpoint ("::ffff:10.0.0.1", 7075), origin));
	ASSERT_NE (nullptr, index.track (endpoint ("::ffff:10.0.0.1", 7076), origin));
	ASSERT_EQ (2, index.count_per_ip (endpoint ("::ffff:10.0.0.1").address (), origin));
	ASSERT_EQ (0, index.count_per_ip (endpoint ("::ffff:10.0.0.2").address (), origin));

	// Two hosts of one /48 are one IP, a host of the next /48 is another
	ASSERT_NE (nullptr, index.track (endpoint ("2001:db8:1:1::1"), origin + 5s));
	ASSERT_NE (nullptr, index.track (endpoint ("2001:db8:1:2::1"), origin + 5s));
	ASSERT_EQ (2, index.count_per_ip (endpoint ("2001:db8:1:ffff::1").address (), origin + 5s));
	ASSERT_EQ (0, index.count_per_ip (endpoint ("2001:db8:2::1").address (), origin + 5s));

	// Once their holds expire the IPv4 reachouts stop counting, though all four are still remembered
	ASSERT_EQ (0, index.count_per_ip (endpoint ("::ffff:10.0.0.1").address (), origin + 10s));
	ASSERT_EQ (2, index.count_per_ip (endpoint ("2001:db8:1:ffff::1").address (), origin + 10s));
	ASSERT_EQ (4, index.size ());
}

/*
 * Reachouts holding their endpoints count per subnetwork, an IPv4 /24 or an IPv6 /32; an ended reachout stops counting at its expiry while it is still remembered
 */
TEST (reachout_index, count_per_subnetwork)
{
	reachout_index index{ test_limits () };

	// Two addresses of one IPv4 /24 count for the whole /24, the next /24 is apart
	ASSERT_NE (nullptr, index.track (endpoint ("::ffff:10.0.0.1"), origin));
	ASSERT_NE (nullptr, index.track (endpoint ("::ffff:10.0.0.2"), origin));
	ASSERT_EQ (2, index.count_per_subnetwork (endpoint ("::ffff:10.0.0.200").address (), origin));
	ASSERT_EQ (0, index.count_per_subnetwork (endpoint ("::ffff:10.0.1.1").address (), origin));

	// Two /48s of one IPv6 /32 count for the whole /32, the next /32 is apart
	ASSERT_NE (nullptr, index.track (endpoint ("2001:db8:1::1"), origin + 5s));
	ASSERT_NE (nullptr, index.track (endpoint ("2001:db8:2::1"), origin + 5s));
	ASSERT_EQ (2, index.count_per_subnetwork (endpoint ("2001:db8:ffff::1").address (), origin + 5s));
	ASSERT_EQ (0, index.count_per_subnetwork (endpoint ("2001:db9::1").address (), origin + 5s));

	// Once their holds expire the IPv4 reachouts stop counting, though all four are still remembered
	ASSERT_EQ (0, index.count_per_subnetwork (endpoint ("::ffff:10.0.0.200").address (), origin + 10s));
	ASSERT_EQ (2, index.count_per_subnetwork (endpoint ("2001:db8:ffff::1").address (), origin + 10s));
	ASSERT_EQ (4, index.size ());
}

/*
 * A purge keeps an expired reachout for its failures until the longest hold has passed after its expiry, then forgets it, so a later reachout to that endpoint starts from no failures
 */
TEST (reachout_index, purge)
{
	reachout_index index{ test_limits () };
	auto const early = endpoint ("::ffff:10.0.0.1");
	auto const late = endpoint ("::ffff:10.0.0.2");
	ASSERT_NE (nullptr, index.track (early, origin));
	ASSERT_NE (nullptr, index.track (late, origin + 5s));

	// Up to the moment before the longest hold has passed after the first expiry both are remembered
	ASSERT_EQ (0, index.purge (origin + 49s));
	ASSERT_EQ (2, index.size ());

	// Then the earlier one is forgotten and the later one stays
	ASSERT_EQ (0, index.purge (origin + 50s));
	ASSERT_EQ (1, index.size ());

	// The forgotten endpoint starts over, the remembered one carries its failure
	auto const restarted = index.track (early, origin + 50s);
	ASSERT_NE (nullptr, restarted);
	ASSERT_EQ (0, restarted->failures);
	auto const continued = index.track (late, origin + 50s);
	ASSERT_NE (nullptr, continued);
	ASSERT_EQ (1, continued->failures);
}

/*
 * Beyond the size limit a purge evicts the reachouts expiring first and reports how many went, a track alone never evicts.
 * A reachout whose attempt still runs is passed over, so its endpoint stays held
 */
TEST (reachout_index, size_limit)
{
	auto limits = test_limits ();
	limits.max_size = 2;
	reachout_index index{ limits };
	auto const first = endpoint ("::ffff:10.0.0.1");
	auto const second = endpoint ("::ffff:10.0.0.2");
	auto const third = endpoint ("::ffff:10.0.0.3");

	// Three reachouts fit until the next purge, the first still running
	auto const running = index.track (first, origin);
	ASSERT_NE (nullptr, running);
	ASSERT_NE (nullptr, index.track (second, origin + 2s));
	ASSERT_NE (nullptr, index.track (third, origin + 1s));
	ASSERT_EQ (3, index.size ());

	// The purge passes over the running one, though it expires first, and evicts the next to expire
	ASSERT_EQ (1, index.purge (origin + 3s));
	ASSERT_EQ (2, index.size ());

	// The evicted endpoint is free again, the others are still held
	auto const replacement = index.track (third, origin + 3s);
	ASSERT_NE (nullptr, replacement);
	ASSERT_EQ (0, replacement->failures);
	ASSERT_EQ (nullptr, index.track (first, origin + 3s));
	ASSERT_EQ (nullptr, index.track (second, origin + 3s));
}

/*
 * With the cooldown at zero a reachout holds its endpoint only while its attempt runs: once it has ended the endpoint can be reached out to again at once and nothing counts against its IP
 */
TEST (reachout_index, cooldown_disabled)
{
	auto limits = test_limits ();
	limits.cooldown = 0s;
	reachout_index index{ limits };
	auto const peer = endpoint ("::ffff:10.0.0.1");

	// The running attempt holds the endpoint though its expiry is its start
	auto reachout = index.track (peer, origin);
	ASSERT_NE (nullptr, reachout);
	ASSERT_EQ (origin, reachout->expiry);
	ASSERT_EQ (nullptr, index.track (peer, origin));
	ASSERT_EQ (1, index.count_per_ip (peer.address (), origin));

	// Ended, it holds nothing
	reachout.reset ();
	ASSERT_EQ (0, index.count_per_ip (peer.address (), origin));
	auto const again = index.track (peer, origin);
	ASSERT_NE (nullptr, again);
	ASSERT_EQ (1, again->failures);
	ASSERT_EQ (origin, again->expiry);
	ASSERT_EQ (1, index.size ());
}

/*
 * A purge never forgets or evicts a reachout whose attempt still runs, however long ago it expired and however far over the size limit the index is, and forgets it once the attempt has ended
 */
TEST (reachout_index, purge_spares_running)
{
	auto limits = test_limits ();
	limits.max_size = 1;
	reachout_index index{ limits };
	auto const first = endpoint ("::ffff:10.0.0.1");
	auto const second = endpoint ("::ffff:10.0.0.2");
	auto first_reachout = index.track (first, origin);
	ASSERT_NE (nullptr, first_reachout);
	auto second_reachout = index.track (second, origin);
	ASSERT_NE (nullptr, second_reachout);

	// Long past the time an ended reachout is remembered, both stay over the limit and hold their endpoints
	ASSERT_EQ (0, index.purge (origin + 1h));
	ASSERT_EQ (2, index.size ());
	ASSERT_EQ (nullptr, index.track (first, origin + 1h));
	ASSERT_EQ (nullptr, index.track (second, origin + 1h));

	// Once their attempts have ended the next purge forgets them
	first_reachout.reset ();
	second_reachout.reset ();
	ASSERT_EQ (0, index.purge (origin + 1h));
	ASSERT_EQ (0, index.size ());
}
