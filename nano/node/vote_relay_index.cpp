#include <nano/lib/utility.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/vote_relay_index.hpp>

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

	std::map<nano::account, query> queries;

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
				auto & query = queries[rep];
				query.rep = rep;
				query.roots_hashes.emplace_back (want.hash, want.root);
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
	for (auto & [rep, query] : queries)
	{
		result.push_back (std::move (query));
	}
	return result;
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

	auto & by_hash = pending.get<tag_hash> ();
	for (auto const & hash : vote->hashes)
	{
		auto [begin, end] = by_hash.equal_range (hash);
		for (auto it = begin; it != end;)
		{
			bool const matches = vote->satisfies (it->finality) && std::find (it->reps.begin (), it->reps.end (), rep) != it->reps.end ();
			if (!matches)
			{
				++it;
				continue;
			}

			auto & completed = touched[{ it->channel, it->id }];
			if (it->reps.size () == 1) // Last missing rep, row is complete
			{
				completed += 1;
				it = by_hash.erase (it);
			}
			else
			{
				by_hash.modify (it, [&rep] (auto & row) {
					std::erase (row.reps, rep);
				});
				++it;
			}
		}
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
	by_request.erase (begin, end);
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

bool nano::vote_relay_index::empty () const
{
	return requests.empty ();
}
