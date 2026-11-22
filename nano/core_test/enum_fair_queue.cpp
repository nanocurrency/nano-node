#include <nano/node/enum_fair_queue.hpp>

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

namespace
{
enum class source
{
	live,
	bootstrap,
	unchecked,
};

using queue_type = nano::enum_fair_queue<int, source, /* batch capacity */ 4>;

// A one letter name per source, so a sequence of pops reads as a string
char letter (source key)
{
	switch (key)
	{
		case source::live:
			return 'l';
		case source::bootstrap:
			return 'b';
		case source::unchecked:
			return 'u';
	}
	return '?';
}
}

/*
 * Every key's cap and share are asked once, at construction, and every key starts with an empty queue
 */
TEST (enum_fair_queue, construction)
{
	std::map<source, int> max_size_asked;
	std::map<source, int> priority_asked;
	queue_type queue{
		[&max_size_asked] (source key) {
			++max_size_asked[key];
			return size_t{ 8 };
		},
		[&priority_asked] (source key) {
			++priority_asked[key];
			return size_t{ 1 };
		}
	};

	// Each of the three keys was asked once for each
	ASSERT_EQ (max_size_asked.size (), 3);
	ASSERT_EQ (priority_asked.size (), 3);
	for (auto key : { source::live, source::bootstrap, source::unchecked })
	{
		ASSERT_EQ (max_size_asked[key], 1);
		ASSERT_EQ (priority_asked[key], 1);
		ASSERT_EQ (queue.size (key), 0);
	}
	ASSERT_EQ (queue.size (), 0);
	ASSERT_TRUE (queue.empty ());

	// Queuing and popping asks for nothing more
	ASSERT_TRUE (queue.push (7, source::live));
	ASSERT_EQ (queue.next ().first, 7);
	ASSERT_EQ (max_size_asked[source::live], 1);
	ASSERT_EQ (priority_asked[source::live], 1);
}

/*
 * Each key's queue is capped by the cap given for that key: a full key drops what is pushed while the others still take requests, and a key with a cap of zero takes nothing
 */
TEST (enum_fair_queue, push_full)
{
	queue_type queue{
		[] (source key) -> size_t {
			switch (key)
			{
				case source::live:
					return 2;
				case source::bootstrap:
					return 1;
				case source::unchecked:
					return 0;
			}
			return 0;
		},
		[] (source) { return size_t{ 1 }; }
	};

	// Only unchecked, with a cap of zero, is full before anything is queued
	ASSERT_FALSE (queue.full (source::live));
	ASSERT_FALSE (queue.full (source::bootstrap));
	ASSERT_TRUE (queue.full (source::unchecked));

	// Live takes two, bootstrap one, unchecked none
	ASSERT_TRUE (queue.push (1, source::live));
	ASSERT_TRUE (queue.push (2, source::live));
	ASSERT_FALSE (queue.push (3, source::live));
	ASSERT_TRUE (queue.push (4, source::bootstrap));
	ASSERT_FALSE (queue.push (5, source::bootstrap));
	ASSERT_FALSE (queue.push (6, source::unchecked));

	// A key at its cap reports full
	ASSERT_TRUE (queue.full (source::live));
	ASSERT_TRUE (queue.full (source::bootstrap));

	// Only the requests that were taken are counted
	ASSERT_EQ (queue.size (source::live), 2);
	ASSERT_EQ (queue.size (source::bootstrap), 1);
	ASSERT_EQ (queue.size (source::unchecked), 0);
	ASSERT_EQ (queue.size (), 3);

	// Popping makes room again
	ASSERT_EQ (queue.next ().first, 1);
	ASSERT_FALSE (queue.full (source::live));
	ASSERT_TRUE (queue.push (3, source::live));
	ASSERT_EQ (queue.size (source::live), 2);
}

/*
 * The shares given at construction decide how many requests each key takes per turn, and each request comes out with its key in arrival order within that key
 */
TEST (enum_fair_queue, priorities)
{
	queue_type queue{
		[] (source) { return size_t{ 999 }; },
		[] (source key) -> size_t { return key == source::live ? 3 : 1; }
	};

	// Live and bootstrap both hold a backlog
	for (int n = 0; n < 6; ++n)
	{
		ASSERT_TRUE (queue.push (n, source::live));
		ASSERT_TRUE (queue.push (100 + n, source::bootstrap));
	}

	// Two rounds of three live requests and one bootstrap request, each key in its own arrival order
	std::string order;
	std::vector<int> live;
	std::vector<int> bootstrap;
	for (int n = 0; n < 8; ++n)
	{
		auto const [value, key] = queue.next ();
		order += letter (key);
		(key == source::live ? live : bootstrap).push_back (value);
	}
	ASSERT_EQ (order, "lllblllb");
	ASSERT_EQ (live, (std::vector<int>{ 0, 1, 2, 3, 4, 5 }));
	ASSERT_EQ (bootstrap, (std::vector<int>{ 100, 101 }));
}

/*
 * Keys are served in the order their queues filled, not in the enum's order
 */
TEST (enum_fair_queue, fill_order)
{
	queue_type queue{
		[] (source) { return size_t{ 999 }; },
		[] (source) { return size_t{ 1 }; }
	};

	// Filled in the reverse of the enum's order
	ASSERT_TRUE (queue.push (1, source::unchecked));
	ASSERT_TRUE (queue.push (2, source::bootstrap));
	ASSERT_TRUE (queue.push (3, source::live));

	// The round goes in fill order
	ASSERT_EQ (queue.next ().second, source::unchecked);
	ASSERT_EQ (queue.next ().second, source::bootstrap);
	ASSERT_EQ (queue.next ().second, source::live);
	ASSERT_TRUE (queue.empty ());
}

/*
 * A batch takes up to the requested count in round robin order and stops early when the queue runs dry, also when more is asked for than the batch holds inline
 */
TEST (enum_fair_queue, next_batch)
{
	queue_type queue{
		[] (source) { return size_t{ 999 }; },
		[] (source) { return size_t{ 1 }; }
	};

	// Three keys holding three requests each
	for (int n = 0; n < 3; ++n)
	{
		ASSERT_TRUE (queue.push (n, source::live));
		ASSERT_TRUE (queue.push (10 + n, source::bootstrap));
		ASSERT_TRUE (queue.push (20 + n, source::unchecked));
	}

	// A batch of two takes the first two keys' turns
	auto const first = queue.next_batch (2);
	ASSERT_EQ (first.size (), 2);
	ASSERT_EQ (first[0].first, 0);
	ASSERT_EQ (first[1].first, 10);
	ASSERT_EQ (queue.size (), 7);

	// A batch of six, more than the four held inline, carries on with the round
	auto const second = queue.next_batch (6);
	ASSERT_EQ (second.size (), 6);
	std::string order;
	for (auto const & [value, key] : second)
	{
		order += letter (key);
	}
	ASSERT_EQ (order, "ulbulb");

	// The last request comes out of a batch that asked for more
	auto const third = queue.next_batch (4);
	ASSERT_EQ (third.size (), 1);
	ASSERT_EQ (third[0].first, 22);
	ASSERT_TRUE (queue.empty ());

	// An empty queue gives an empty batch
	ASSERT_TRUE (queue.next_batch (4).empty ());
}
