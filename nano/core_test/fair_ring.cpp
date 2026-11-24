#include <nano/node/fair_ring.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace
{
using ring_type = nano::fair_ring<int, char>;
using lane_type = ring_type::lane_type;
}

/*
 * A ring without lanes holds nothing and a ready pop finds nothing to serve
 */
TEST (fair_ring, construction)
{
	ring_type ring;
	ASSERT_EQ (ring.size (), 0);
	ASSERT_TRUE (ring.empty ());
	ASSERT_FALSE (ring.next_ready ([] (char) { return true; }).has_value ());
}

/*
 * A lane's requests come out in the order they were pushed, each with the lane's key, and the sizes of the lane and the ring follow every push and pop
 */
TEST (fair_ring, fifo)
{
	lane_type lane{ 'a', /* max size */ 999, /* priority */ 1 };
	ring_type ring;

	// Three requests in one lane
	ASSERT_TRUE (ring.push (lane, 7));
	ASSERT_TRUE (ring.push (lane, 8));
	ASSERT_TRUE (ring.push (lane, 9));
	ASSERT_EQ (lane.size (), 3);
	ASSERT_EQ (ring.size (), 3);

	// The oldest comes out first, tagged with the lane's key
	auto const first = ring.next ();
	ASSERT_EQ (first.first, 7);
	ASSERT_EQ (first.second, 'a');
	ASSERT_EQ (lane.size (), 2);
	ASSERT_EQ (ring.size (), 2);

	// The rest follow in order until both are empty
	ASSERT_EQ (ring.next ().first, 8);
	ASSERT_EQ (ring.next ().first, 9);
	ASSERT_TRUE (lane.empty ());
	ASSERT_TRUE (ring.empty ());
}

/*
 * A full lane drops what is pushed into it without changing the ring's size while other lanes still take requests, and a lane with a cap of zero takes nothing at all
 */
TEST (fair_ring, push_full)
{
	lane_type small{ 'a', /* max size */ 2, /* priority */ 1 };
	lane_type closed{ 'b', /* max size */ 0, /* priority */ 1 };
	lane_type other{ 'c', /* max size */ 999, /* priority */ 1 };
	ring_type ring;

	// The small lane takes two and drops the third
	ASSERT_TRUE (ring.push (small, 1));
	ASSERT_FALSE (small.full ());
	ASSERT_TRUE (ring.push (small, 2));
	ASSERT_TRUE (small.full ());
	ASSERT_FALSE (ring.push (small, 3));
	ASSERT_EQ (small.size (), 2);
	ASSERT_EQ (ring.size (), 2);

	// The closed lane is full from the start, drops everything and never joins the ring
	ASSERT_TRUE (closed.full ());
	ASSERT_FALSE (ring.push (closed, 4));
	ASSERT_TRUE (closed.empty ());
	ASSERT_EQ (ring.size (), 2);

	// Another lane is unaffected by the full one
	ASSERT_TRUE (ring.push (other, 5));
	ASSERT_EQ (ring.size (), 3);

	// The dropped requests never come out
	ASSERT_EQ (ring.next ().first, 1);
	ASSERT_EQ (ring.next ().first, 5);
	ASSERT_EQ (ring.next ().first, 2);
	ASSERT_TRUE (ring.empty ());
}

/*
 * Each lane takes its priority's worth of requests per turn before the turn passes on, so lanes with shares of 3 and 1 go three to one
 */
TEST (fair_ring, shares)
{
	lane_type heavy{ 'h', /* max size */ 999, /* priority */ 3 };
	lane_type light{ 'l', /* max size */ 999, /* priority */ 1 };
	ring_type ring;

	// Both lanes hold a backlog
	for (int n = 0; n < 6; ++n)
	{
		ASSERT_TRUE (ring.push (heavy, n));
		ASSERT_TRUE (ring.push (light, 100 + n));
	}

	// Two rounds of three requests from heavy and one from light
	std::string order;
	for (int n = 0; n < 8; ++n)
	{
		order += ring.next ().second;
	}
	ASSERT_EQ (order, "hhhlhhhl");
}

/*
 * A share of 0 takes one request per turn like a share of 1, so a lane given no share is still served
 */
TEST (fair_ring, zero_share)
{
	lane_type zero{ 'z', /* max size */ 999, /* priority */ 0 };
	lane_type one{ 'o', /* max size */ 999, /* priority */ 1 };
	ring_type ring;

	// Both lanes hold three requests, the zero share lane filled first
	for (int n = 0; n < 3; ++n)
	{
		ASSERT_TRUE (ring.push (zero, n));
		ASSERT_TRUE (ring.push (one, 100 + n));
	}

	// The lanes alternate one request at a time
	std::string order;
	while (!ring.empty ())
	{
		order += ring.next ().second;
	}
	ASSERT_EQ (order, "zozozo");
}

/*
 * Lanes are served in the order they filled, not in the order they were made or by their keys
 */
TEST (fair_ring, fill_order)
{
	lane_type a{ 'a', /* max size */ 999, /* priority */ 1 };
	lane_type b{ 'b', /* max size */ 999, /* priority */ 1 };
	lane_type c{ 'c', /* max size */ 999, /* priority */ 1 };
	ring_type ring;

	// Filled in a different order from the one they were made in
	ASSERT_TRUE (ring.push (c, 1));
	ASSERT_TRUE (ring.push (a, 2));
	ASSERT_TRUE (ring.push (b, 3));

	// The round goes in fill order
	ASSERT_EQ (ring.next ().second, 'c');
	ASSERT_EQ (ring.next ().second, 'a');
	ASSERT_EQ (ring.next ().second, 'b');
	ASSERT_TRUE (ring.empty ());
}

/*
 * A lane drained and filled again joins at the end of the round under way, behind the lanes still waiting for their turn and behind the one whose turn just ended
 */
TEST (fair_ring, refill_goes_last)
{
	lane_type b{ 'b', /* max size */ 999, /* priority */ 1 };
	lane_type l{ 'l', /* max size */ 999, /* priority */ 1 };
	lane_type u{ 'u', /* max size */ 999, /* priority */ 1 };
	ring_type ring;

	ASSERT_TRUE (ring.push (b, 7));
	ASSERT_TRUE (ring.push (l, 8));
	ASSERT_TRUE (ring.push (l, 9));
	ASSERT_TRUE (ring.push (u, 10));
	ASSERT_TRUE (ring.push (u, 11));

	// B drains with its only request and l takes its turn, which passes the round on to u
	ASSERT_EQ (ring.next ().first, 7);
	ASSERT_EQ (ring.next ().first, 8);

	// B fills again and joins at the end of the round: behind u, which has not had its turn, and behind l, whose turn is just over
	ASSERT_TRUE (ring.push (b, 12));
	ASSERT_EQ (ring.next ().first, 10);
	ASSERT_EQ (ring.next ().first, 9);
	ASSERT_EQ (ring.next ().first, 12);
	ASSERT_EQ (ring.next ().first, 11);
	ASSERT_TRUE (ring.empty ());
}

/*
 * Lanes that drain and fill again after every request they give cannot keep a lane with a backlog from its turn, since a refill waits for the round under way to end
 */
TEST (fair_ring, refill_cannot_starve)
{
	lane_type backlog{ 'l', /* max size */ 999, /* priority */ 1 };
	std::deque<lane_type> refilling;
	refilling.emplace_back ('b', /* max size */ 999, /* priority */ 1);
	refilling.emplace_back ('u', /* max size */ 999, /* priority */ 1);
	refilling.emplace_back ('x', /* max size */ 999, /* priority */ 1);
	ring_type ring;

	// One lane holds a backlog, three others one request each
	for (int n = 0; n < 10; ++n)
	{
		ASSERT_TRUE (ring.push (backlog, n));
	}
	for (auto & lane : refilling)
	{
		ASSERT_TRUE (ring.push (lane, 100));
	}

	// Every request a refilling lane gives is pushed back at once, so there is always fresh work joining the ring
	std::map<char, int> turns;
	for (int n = 0; n < 12; ++n)
	{
		auto const [value, key] = ring.next ();
		++turns[key];
		auto const refill = std::ranges::find_if (refilling, [key] (lane_type const & lane) { return lane.key == key; });
		if (refill != refilling.end ())
		{
			ASSERT_TRUE (ring.push (*refill, value));
		}
	}

	// Twelve requests are three turns per lane, the backlog included
	ASSERT_EQ (turns['l'], 3);
	ASSERT_EQ (turns['b'], 3);
	ASSERT_EQ (turns['u'], 3);
	ASSERT_EQ (turns['x'], 3);
}

/*
 * An evicting push into a full lane drops the lane's oldest request and hands it back with the ring's size unchanged, while a lane with room queues the request as push does and joins the ring when it was empty
 */
TEST (fair_ring, push_evicting)
{
	lane_type lane{ 'a', /* max size */ 2, /* priority */ 1 };
	ring_type ring;

	// With room the request is queued and nothing is evicted, the first one bringing the lane into the ring
	ASSERT_FALSE (ring.push_evicting (lane, 1).has_value ());
	ASSERT_FALSE (ring.push_evicting (lane, 2).has_value ());
	ASSERT_EQ (ring.size (), 2);

	// The full lane gives up its oldest request for the new one
	auto const evicted = ring.push_evicting (lane, 3);
	ASSERT_TRUE (evicted.has_value ());
	ASSERT_EQ (*evicted, 1);
	ASSERT_EQ (lane.size (), 2);
	ASSERT_EQ (ring.size (), 2);

	// The lane keeps its most recent requests in arrival order
	ASSERT_EQ (ring.next ().first, 2);
	ASSERT_EQ (ring.next ().first, 3);
	ASSERT_TRUE (ring.empty ());
}

/*
 * A ready pop serves the lanes the predicate accepts and leaves the refused ones queued, comes back empty when every lane holding requests is refused, and the refused requests come out in order once accepted
 */
TEST (fair_ring, next_ready_skips_refused)
{
	lane_type a{ 'a', /* max size */ 999, /* priority */ 1 };
	lane_type b{ 'b', /* max size */ 999, /* priority */ 1 };
	ring_type ring;

	ASSERT_TRUE (ring.push (a, 1));
	ASSERT_TRUE (ring.push (a, 2));
	ASSERT_TRUE (ring.push (b, 11));
	ASSERT_TRUE (ring.push (b, 12));

	// With a refused, b is served although a's turn comes first
	auto const not_a = [] (char key) { return key != 'a'; };
	auto const first = ring.next_ready (not_a);
	ASSERT_TRUE (first.has_value ());
	ASSERT_EQ (first->first, 11);
	auto const second = ring.next_ready (not_a);
	ASSERT_TRUE (second.has_value ());
	ASSERT_EQ (second->first, 12);

	// Only a holds requests and it is refused, so nothing comes out and its requests stay
	ASSERT_FALSE (ring.next_ready (not_a).has_value ());
	ASSERT_EQ (ring.size (), 2);
	ASSERT_EQ (a.size (), 2);

	// Accepted again, a gives its requests in arrival order
	auto const any = [] (char) { return true; };
	ASSERT_EQ (ring.next_ready (any)->first, 1);
	ASSERT_EQ (ring.next_ready (any)->first, 2);
	ASSERT_TRUE (ring.empty ());
}

/*
 * A ready pop that finds nothing leaves the round where it was, the share count included, and a lane taking over the turn from a refused one starts a full share of its own
 */
TEST (fair_ring, next_ready_keeps_shares)
{
	lane_type a{ 'a', /* max size */ 999, /* priority */ 2 };
	lane_type b{ 'b', /* max size */ 999, /* priority */ 2 };
	ring_type ring;
	for (int n = 0; n < 4; ++n)
	{
		ASSERT_TRUE (ring.push (a, 1 + n));
		ASSERT_TRUE (ring.push (b, 11 + n));
	}

	// A takes the first request of its share, then a ready pop refusing everything finds nothing
	ASSERT_EQ (ring.next ().first, 1);
	ASSERT_FALSE (ring.next_ready ([] (char) { return false; }).has_value ());

	// A finishes the share it was in, the empty ready pop changed nothing
	ASSERT_EQ (ring.next ().first, 2);
	ASSERT_EQ (ring.next ().first, 11);

	// With b refused in the middle of its turn, a takes over with a full share of two
	auto const popped = ring.next_ready ([] (char key) { return key != 'b'; });
	ASSERT_TRUE (popped.has_value ());
	ASSERT_EQ (popped->first, 3);
	ASSERT_EQ (ring.next ().first, 4);

	// A is drained, b takes a full share and then the rest of its backlog
	ASSERT_EQ (ring.next ().first, 12);
	ASSERT_EQ (ring.next ().first, 13);
	ASSERT_EQ (ring.next ().first, 14);
	ASSERT_TRUE (ring.empty ());
}

/*
 * Discarding the lane whose turn it is drops its requests and passes the turn to the next lane, which takes a full share of its own
 */
TEST (fair_ring, discard_current)
{
	lane_type a{ 'a', /* max size */ 999, /* priority */ 2 };
	lane_type b{ 'b', /* max size */ 999, /* priority */ 2 };
	lane_type c{ 'c', /* max size */ 999, /* priority */ 2 };
	ring_type ring;
	for (int n = 0; n < 4; ++n)
	{
		ASSERT_TRUE (ring.push (a, 1 + n));
		ASSERT_TRUE (ring.push (b, 11 + n));
		ASSERT_TRUE (ring.push (c, 21 + n));
	}

	// A is in the middle of its share when it is discarded with the three requests it still holds
	ASSERT_EQ (ring.next ().first, 1);
	ASSERT_EQ (ring.discard (a), 3);
	ASSERT_TRUE (a.empty ());
	ASSERT_EQ (ring.size (), 8);

	// B takes the turn with a share of two, then c
	ASSERT_EQ (ring.next ().first, 11);
	ASSERT_EQ (ring.next ().first, 12);
	ASSERT_EQ (ring.next ().first, 21);
	ASSERT_EQ (ring.next ().first, 22);
	ASSERT_EQ (ring.next ().first, 13);
}

/*
 * Discarding a lane that waits for its turn drops its requests while the lane whose turn it is keeps the rest of its share
 */
TEST (fair_ring, discard_waiting)
{
	lane_type a{ 'a', /* max size */ 999, /* priority */ 2 };
	lane_type b{ 'b', /* max size */ 999, /* priority */ 2 };
	lane_type c{ 'c', /* max size */ 999, /* priority */ 2 };
	ring_type ring;
	for (int n = 0; n < 4; ++n)
	{
		ASSERT_TRUE (ring.push (a, 1 + n));
		ASSERT_TRUE (ring.push (b, 11 + n));
		ASSERT_TRUE (ring.push (c, 21 + n));
	}

	// A is in the middle of its share when c, which waits, is discarded
	ASSERT_EQ (ring.next ().first, 1);
	ASSERT_EQ (ring.discard (c), 4);
	ASSERT_TRUE (c.empty ());
	ASSERT_EQ (ring.size (), 7);

	// A finishes its share and the round goes on to b and back to a, without c
	ASSERT_EQ (ring.next ().first, 2);
	ASSERT_EQ (ring.next ().first, 11);
	ASSERT_EQ (ring.next ().first, 12);
	ASSERT_EQ (ring.next ().first, 3);
}

/*
 * Discarding a lane with nothing queued drops nothing and leaves the round alone, and a discarded lane takes requests again, joining the round under way like any refilled lane
 */
TEST (fair_ring, discard_rejoin)
{
	lane_type a{ 'a', /* max size */ 999, /* priority */ 1 };
	lane_type b{ 'b', /* max size */ 999, /* priority */ 1 };
	lane_type idle{ 'i', /* max size */ 999, /* priority */ 1 };
	ring_type ring;
	ASSERT_TRUE (ring.push (a, 1));
	ASSERT_TRUE (ring.push (a, 2));
	ASSERT_TRUE (ring.push (b, 11));
	ASSERT_TRUE (ring.push (b, 12));

	// A has had its turn when the idle lane is discarded, which drops nothing
	ASSERT_EQ (ring.next ().first, 1);
	ASSERT_EQ (ring.discard (idle), 0);
	ASSERT_EQ (ring.size (), 3);

	// A is discarded while it waits, then takes a new request and joins at the end of the round, behind b
	ASSERT_EQ (ring.discard (a), 1);
	ASSERT_TRUE (ring.push (a, 3));
	ASSERT_EQ (ring.next ().first, 11);
	ASSERT_EQ (ring.next ().first, 3);
	ASSERT_EQ (ring.next ().first, 12);
	ASSERT_TRUE (ring.empty ());
}

/*
 * Clearing in the middle of a round drops the requests of every lane, and what is pushed afterwards starts a round of its own in fill order
 */
TEST (fair_ring, clear)
{
	lane_type a{ 'a', /* max size */ 999, /* priority */ 2 };
	lane_type b{ 'b', /* max size */ 999, /* priority */ 2 };
	ring_type ring;
	for (int n = 0; n < 3; ++n)
	{
		ASSERT_TRUE (ring.push (a, 1 + n));
		ASSERT_TRUE (ring.push (b, 11 + n));
	}

	// A is in the middle of its share when everything is cleared
	ASSERT_EQ (ring.next ().first, 1);
	ring.clear ();
	ASSERT_TRUE (ring.empty ());
	ASSERT_TRUE (a.empty ());
	ASSERT_TRUE (b.empty ());
	ASSERT_FALSE (ring.next_ready ([] (char) { return true; }).has_value ());

	// The next round starts from scratch in the new fill order, b first
	ASSERT_TRUE (ring.push (b, 20));
	ASSERT_TRUE (ring.push (a, 10));
	ASSERT_EQ (ring.next ().first, 20);
	ASSERT_EQ (ring.next ().first, 10);
	ASSERT_TRUE (ring.empty ());
}

/*
 * A ring destroyed while a lane still holds requests drops them, so the lane outlives it empty and a new ring serves it from scratch
 */
TEST (fair_ring, lane_outlives_ring)
{
	lane_type lane{ 'a', /* max size */ 999, /* priority */ 1 };

	// The first ring goes away with two requests queued in the lane
	{
		ring_type ring;
		ASSERT_TRUE (ring.push (lane, 1));
		ASSERT_TRUE (ring.push (lane, 2));
	}
	ASSERT_TRUE (lane.empty ());

	// A new ring links the lane on its first push and serves only what it was given
	ring_type ring;
	ASSERT_TRUE (ring.push (lane, 3));
	ASSERT_EQ (ring.size (), 1);
	auto const [request, key] = ring.next ();
	ASSERT_EQ (request, 3);
	ASSERT_EQ (key, 'a');
	ASSERT_TRUE (ring.empty ());
}

/*
 * Random pushes, evicting pushes, pops, ready pops, discards and clears over lanes with different caps and shares, checked after every step against a model of what each lane holds
 * Every request must come out once, in its lane's order and with its lane's key, a dropped, evicted or discarded one never, and the sizes must agree with the model throughout
 * Between ready pops, while a lane holds requests no other lane may take more than one turn's worth of requests before it is served
 */
TEST (fair_ring, random)
{
	// Shares of 0 to 3, and small caps so pushes into full lanes happen often, the last lane taking nothing at all
	std::deque<lane_type> lanes;
	lanes.emplace_back ('a', /* max size */ 1, /* priority */ 0);
	lanes.emplace_back ('b', /* max size */ 3, /* priority */ 1);
	lanes.emplace_back ('c', /* max size */ 8, /* priority */ 2);
	lanes.emplace_back ('d', /* max size */ 1000, /* priority */ 3);
	lanes.emplace_back ('e', /* max size */ 0, /* priority */ 1);
	auto const count = lanes.size ();
	ring_type ring; // Declared after its lanes, so it lets go of them before they go

	// What each lane should hold
	std::vector<std::deque<int>> model (count);
	// waited[w][l]: requests lane l gave while lane w held requests, since w was last served
	std::vector<std::vector<size_t>> waited (count, std::vector<size_t> (count, 0));

	std::mt19937 rng{ 7 }; // Fixed, so a failure replays
	int pushed = 0;

	// Takes a served request off the model, where it must be the oldest of the lane its key names
	auto const take = [&] (ring_type::value_type const & entry, size_t & index) {
		index = static_cast<size_t> (entry.second - 'a');
		ASSERT_LT (index, count);
		ASSERT_FALSE (model[index].empty ());
		ASSERT_EQ (model[index].front (), entry.first);
		model[index].pop_front ();
	};

	// Counts a plain pop against every other lane that waits with requests, which must not wait through more than a turn of it
	auto const account_turn = [&] (size_t served) {
		for (size_t w = 0; w < count; ++w)
		{
			if (w != served && !model[w].empty ())
			{
				++waited[w][served];
				ASSERT_LE (waited[w][served], std::max<size_t> (lanes[served].priority, 1)) << "lane " << lanes[w].key << " waited through lane " << lanes[served].key;
			}
		}
		std::ranges::fill (waited[served], 0);
	};

	auto const reset_turns = [&] () {
		for (auto & row : waited)
		{
			std::ranges::fill (row, 0);
		}
	};

	for (int step = 0; step < 20000; ++step)
	{
		auto const action = rng () % 100;
		auto const index = rng () % count;
		auto & lane = lanes[index];

		if (action < 40)
		{
			// A push is taken exactly while the lane has room
			bool const room = model[index].size () < lane.max_size;
			ASSERT_EQ (ring.push (lane, pushed), room);
			if (room)
			{
				model[index].push_back (pushed);
			}
			++pushed;
		}
		else if (action < 50)
		{
			// An evicting push always queues into a lane that takes requests at all, evicting its oldest when full
			if (lane.max_size > 0)
			{
				auto const evicted = ring.push_evicting (lane, pushed);
				if (model[index].size () >= lane.max_size)
				{
					ASSERT_TRUE (evicted.has_value ());
					ASSERT_EQ (*evicted, model[index].front ());
					model[index].pop_front ();
				}
				else
				{
					ASSERT_FALSE (evicted.has_value ());
				}
				model[index].push_back (pushed++);
			}
		}
		else if (action < 80)
		{
			// A plain pop, which keeps every waiting lane within a turn of the others
			if (!ring.empty ())
			{
				size_t served = 0;
				ASSERT_NO_FATAL_FAILURE (take (ring.next (), served));
				ASSERT_NO_FATAL_FAILURE (account_turn (served));
			}
		}
		else if (action < 92)
		{
			// A ready pop over a random set of accepted lanes serves one of them, or finds that every lane holding requests is refused
			auto const accepted = rng ();
			auto const ready = [accepted] (char key) { return ((accepted >> (key - 'a')) & 1) != 0; };
			auto const popped = ring.next_ready (ready);
			if (popped)
			{
				ASSERT_TRUE (ready (popped->second));
				size_t served = 0;
				ASSERT_NO_FATAL_FAILURE (take (*popped, served));
			}
			else
			{
				for (size_t n = 0; n < count; ++n)
				{
					ASSERT_TRUE (model[n].empty () || !ready (lanes[n].key));
				}
			}
			// Skipping refused lanes may serve one ahead of its turn, so the turn bound starts over
			reset_turns ();
		}
		else if (action < 98)
		{
			// A discard drops exactly what the lane holds
			ASSERT_EQ (ring.discard (lane), model[index].size ());
			model[index].clear ();
			std::ranges::fill (waited[index], 0);
		}
		else
		{
			ring.clear ();
			for (auto & requests : model)
			{
				requests.clear ();
			}
			reset_turns ();
		}

		// Every lane and the ring agree with the model
		size_t total = 0;
		for (size_t n = 0; n < count; ++n)
		{
			ASSERT_EQ (lanes[n].size (), model[n].size ());
			total += model[n].size ();
		}
		ASSERT_EQ (ring.size (), total);
		ASSERT_EQ (ring.empty (), total == 0);
	}

	// Whatever is left comes out in order
	while (!ring.empty ())
	{
		size_t served = 0;
		ASSERT_NO_FATAL_FAILURE (take (ring.next (), served));
	}
	for (auto const & requests : model)
	{
		ASSERT_TRUE (requests.empty ());
	}
}

/*
 * Popping from a ring with nothing queued is a caller's bug, the cursor would have no lane to serve, so next () refuses it
 */
TEST (fair_ring_DeathTest, next_empty)
{
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	// A ring that held a request and gave it up
	lane_type lane{ 'a', /* max size */ 999, /* priority */ 1 };
	ring_type ring;
	ASSERT_TRUE (ring.push (lane, 1));
	ASSERT_EQ (ring.next ().first, 1);

	// One more pop must abort
	ASSERT_DEATH ({ ring.next (); }, "!empty \\(\\)");
}

/*
 * A lane destroyed while its ring still serves it would leave the ring pointing at freed memory, so the lane refuses to go before it is drained or discarded
 */
TEST (fair_ring_DeathTest, lane_destroyed_in_ring)
{
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	// The ring outlives a lane that still holds a request
	ring_type ring;
	ASSERT_DEATH ({
		lane_type lane ('a', /* max size */ 999, /* priority */ 1); // Parentheses, a braced list's commas would split the macro's arguments
		ring.push (lane, 1);
	},
	"lane destroyed while its ring still serves it");
}
