#include <nano/lib/utility.hpp>
#include <nano/node/vote_relay_index.hpp>
#include <nano/secure/vote.hpp>

#include <algorithm>
#include <iterator>
#include <map>

nano::vote_relay_index::vote_relay_index (request_limits limits_a) :
	limits{ limits_a }
{
}

std::deque<nano::vote_relay_index::query> nano::vote_relay_index::insert (std::shared_ptr<nano::transport::channel> const & channel, id_t id, std::deque<want> const & wants, nano::vote_finality finality, std::chrono::steady_clock::time_point now)
{
	debug_assert (!wants.empty ());

	// Replace any previous request with the same channel and id
	erase (channel, id);

	std::map<nano::account, query> to_send;

	std::size_t rows{ 0 };
	for (auto const & want : wants)
	{
		debug_assert (!want.reps.empty ());

		// Reps with an in-flight query for this hash will not be queried again, their votes are shared when they arrive
		auto in_flight = in_flight_reps (want.hash);
		for (auto const & rep : want.reps)
		{
			if (!in_flight.contains (rep))
			{
				auto & query = to_send[rep];
				query.rep = rep;
				query.roots_hashes.emplace_back (want.hash, want.root);

				[[maybe_unused]] auto [entry, inserted] = queries.insert ({ want.hash, rep, want.root, now });
				debug_assert (inserted); // A pair nobody waits for is released right away
			}
		}

		auto [it, inserted] = pending.insert ({ channel, id, want.hash, want.root, want.reps, finality });
		debug_assert (inserted); // Caller deduplicates hashes within a request
		rows += inserted ? 1 : 0;
	}
	release_assert (rows > 0);
	request_entry entry{ channel, id, now + limits.lifetime, now + limits.idle_timeout, std::nullopt, {}, {}, rows };
	entry.due = due_of (entry);
	requests.insert (entry);

	std::deque<query> result;
	for (auto & [rep, query] : to_send)
	{
		result.push_back (std::move (query));
	}
	return result;
}

std::deque<nano::vote_relay_index::query> nano::vote_relay_index::requery (std::chrono::steady_clock::time_point now)
{
	std::map<nano::account, query> to_send;

	auto & by_time = queries.get<tag_last_query> ();
	for (auto it = by_time.begin (); it != by_time.end () && it->last_query + limits.requery_interval <= now;)
	{
		// A pair whose requests all end before an answer could arrive is left as it is, a later request for it is asked at the next round
		auto const until = waited_until (it->hash, it->rep);
		if (!until || *until <= now + limits.requery_grace)
		{
			++it;
			continue;
		}
		auto & query = to_send[it->rep];
		query.rep = it->rep;
		query.roots_hashes.emplace_back (it->hash, it->root);
		// Restamping moves the pair behind every due one, so the loop ends
		by_time.modify (it++, [now] (auto & entry) {
			entry.last_query = now;
		});
	}

	std::deque<query> result;
	for (auto & [rep, query] : to_send)
	{
		result.push_back (std::move (query));
	}
	return result;
}

bool nano::vote_relay_index::add_block (nano::root const & root, nano::block_hash const & hash)
{
	// Only roots some row waits for are tracked, so the blocks go away with their rows
	auto const & by_root = pending.get<tag_root> ();
	if (by_root.find (root) == by_root.end ())
	{
		return false;
	}
	return blocks.insert ({ hash, root }).second;
}

std::deque<nano::vote_relay_index::reply> nano::vote_relay_index::vote (std::shared_ptr<nano::vote> const & vote, std::chrono::steady_clock::time_point now)
{
	if (pending.empty ())
	{
		return {};
	}

	auto const rep = vote->account;

	// Requests this vote contributes to, with the number of pending rows it completed
	std::map<std::pair<std::shared_ptr<nano::transport::channel>, id_t>, std::size_t> touched;
	// Rows the vote settled for the rep, by the block they named and their root
	std::deque<std::pair<nano::block_hash, nano::root>> settled;

	// Takes the rep off every row of the range that waits for it and accepts the vote
	auto settle = [&] (auto & rows, auto range) {
		for (auto it = range.first; it != range.second;)
		{
			bool const matches = vote->satisfies (it->finality) && std::find (it->reps.begin (), it->reps.end (), rep) != it->reps.end ();
			if (!matches)
			{
				++it;
				continue;
			}

			settled.emplace_back (it->hash, it->root);
			auto & completed = touched[{ it->channel, it->id }];
			if (it->reps.size () == 1) // Last missing rep, row is complete
			{
				completed += 1;
				it = rows.erase (it);
			}
			else
			{
				rows.modify (it, [&rep] (auto & row) {
					std::erase (row.reps, rep);
				});
				++it;
			}
		}
	};

	auto & by_hash = pending.get<tag_hash> ();
	auto & by_root = pending.get<tag_root> ();
	for (auto const & hash : vote->hashes)
	{
		// Rows whose request named the block
		settle (by_hash, by_hash.equal_range (hash));

		// Rows waiting for a root the caller knows the block at, whatever block they named
		auto [begin, end] = blocks.get<tag_block> ().equal_range (std::make_tuple (hash));
		for (auto block = begin; block != end; ++block)
		{
			settle (by_root, by_root.equal_range (block->root));
		}
	}

	for (auto const & [hash, root] : settled)
	{
		release (hash, { rep });
		release_blocks (root);
	}

	std::deque<reply> result;
	for (auto const & [key, completed] : touched)
	{
		auto const & [channel, id] = key;
		auto it = requests.get<tag_request> ().find (std::make_tuple (channel, id));
		release_assert (it != requests.get<tag_request> ().end ());

		// The vote pushes the idle timeout back and starts the flush interval if nothing was waiting to go out
		requests.get<tag_request> ().modify (it, [&] (auto & request) {
			request.votes.push_back (vote);
			debug_assert (request.pending >= completed);
			request.pending -= completed;
			request.idle_due = now + limits.idle_timeout;
			if (!request.flush_due)
			{
				request.flush_due = now + limits.flush_interval;
			}
			request.due = due_of (request);
		});

		if (it->pending == 0) // All rows answered, request is complete
		{
			result.push_back ({ it->channel, it->id, it->votes, outcome::completed });
			requests.get<tag_request> ().erase (it);
		}
		else if (it->votes.size () >= limits.flush_threshold) // Enough to go out without waiting for the interval
		{
			result.push_back ({ it->channel, it->id, it->votes, outcome::flush });
			requests.get<tag_request> ().modify (it, [] (auto & request) {
				request.votes.clear ();
				request.flush_due.reset ();
				request.due = due_of (request);
			});
		}
	}
	return result;
}

std::deque<nano::vote_relay_index::reply> nano::vote_relay_index::due (std::chrono::steady_clock::time_point now)
{
	std::deque<reply> result;
	auto & by_due = requests.get<tag_due> ();
	// Every round either removes the first request or pushes its due time past now, so the loop ends
	while (!by_due.empty () && by_due.begin ()->due <= now)
	{
		auto it = by_due.begin ();
		if (it->expiry <= now || it->idle_due <= now)
		{
			result.push_back ({ it->channel, it->id, it->votes, it->expiry <= now ? outcome::expired : outcome::idle });
			erase_pending (it->channel, it->id);
			by_due.erase (it);
		}
		else // The flush interval ran out
		{
			debug_assert (it->flush_due && *it->flush_due <= now && !it->votes.empty ());
			result.push_back ({ it->channel, it->id, it->votes, outcome::flush });
			by_due.modify (it, [] (auto & request) {
				request.votes.clear ();
				request.flush_due.reset ();
				request.due = due_of (request);
			});
		}
	}
	return result;
}

bool nano::vote_relay_index::erase (std::shared_ptr<nano::transport::channel> const & channel, id_t id)
{
	auto & by_request = requests.get<tag_request> ();
	auto it = by_request.find (std::make_tuple (channel, id));
	if (it == by_request.end ())
	{
		return false;
	}
	erase_pending (channel, id);
	by_request.erase (it);
	return true;
}

void nano::vote_relay_index::erase_pending (std::shared_ptr<nano::transport::channel> const & channel, id_t id)
{
	auto & by_request = pending.get<tag_request> ();
	auto [begin, end] = by_request.equal_range (std::make_tuple (channel, id));
	std::deque<std::pair<nano::block_hash, std::deque<nano::account>>> released;
	std::deque<nano::root> roots;
	for (auto it = begin; it != end; ++it)
	{
		released.emplace_back (it->hash, it->reps);
		roots.push_back (it->root);
	}
	by_request.erase (begin, end);
	for (auto const & [hash, reps] : released)
	{
		release (hash, reps);
	}
	for (auto const & root : roots)
	{
		release_blocks (root);
	}
}

void nano::vote_relay_index::release_blocks (nano::root const & root)
{
	auto const & by_root = pending.get<tag_root> ();
	if (by_root.find (root) == by_root.end ())
	{
		blocks.get<tag_root> ().erase (root);
	}
}

void nano::vote_relay_index::release (nano::block_hash const & hash, std::deque<nano::account> const & reps)
{
	auto const in_flight = in_flight_reps (hash);
	auto & by_pair = queries.get<tag_pair> ();
	for (auto const & rep : reps)
	{
		if (!in_flight.contains (rep))
		{
			if (auto it = by_pair.find (std::make_tuple (hash, rep)); it != by_pair.end ())
			{
				by_pair.erase (it);
			}
		}
	}
}

std::optional<std::chrono::steady_clock::time_point> nano::vote_relay_index::waited_until (nano::block_hash const & hash, nano::account const & rep) const
{
	std::optional<std::chrono::steady_clock::time_point> result;
	auto const & by_request = requests.get<tag_request> ();
	auto [begin, end] = pending.get<tag_hash> ().equal_range (hash);
	for (auto row = begin; row != end; ++row)
	{
		if (std::find (row->reps.begin (), row->reps.end (), rep) == row->reps.end ())
		{
			continue;
		}
		auto request = by_request.find (std::make_tuple (row->channel, row->id));
		release_assert (request != by_request.end ());
		// The request ends at its lifetime or its idle timeout, whichever comes first, a flush does not end it
		auto const ends = std::min (request->expiry, request->idle_due);
		result = result ? std::max (*result, ends) : ends;
	}
	return result;
}

std::unordered_set<nano::account> nano::vote_relay_index::in_flight_reps (nano::block_hash const & hash) const
{
	std::unordered_set<nano::account> result;
	auto [begin, end] = pending.get<tag_hash> ().equal_range (hash);
	for (auto it = begin; it != end; ++it)
	{
		result.insert (it->reps.begin (), it->reps.end ());
	}
	return result;
}

void nano::vote_relay_index::clear ()
{
	requests.clear ();
	pending.clear ();
	blocks.clear ();
	queries.clear ();
}

std::optional<std::chrono::steady_clock::time_point> nano::vote_relay_index::next_due () const
{
	auto const & by_due = requests.get<tag_due> ();
	if (by_due.empty ())
	{
		return std::nullopt;
	}
	return by_due.begin ()->due;
}

std::optional<std::chrono::steady_clock::time_point> nano::vote_relay_index::next_requery_due () const
{
	auto const & by_time = queries.get<tag_last_query> ();
	if (by_time.empty ())
	{
		return std::nullopt;
	}
	return by_time.begin ()->last_query + limits.requery_interval;
}

std::chrono::steady_clock::time_point nano::vote_relay_index::due_of (request_entry const & request)
{
	auto due = std::min (request.expiry, request.idle_due);
	if (request.flush_due)
	{
		due = std::min (due, *request.flush_due);
	}
	return due;
}

std::size_t nano::vote_relay_index::size () const
{
	return requests.size ();
}

std::size_t nano::vote_relay_index::size (std::shared_ptr<nano::transport::channel> const & channel) const
{
	auto [begin, end] = requests.get<tag_request> ().equal_range (std::make_tuple (channel));
	return std::distance (begin, end);
}

std::size_t nano::vote_relay_index::pending_size () const
{
	return pending.size ();
}

std::size_t nano::vote_relay_index::blocks_size () const
{
	return blocks.size ();
}

bool nano::vote_relay_index::empty () const
{
	return requests.empty ();
}
