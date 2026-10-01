#include <nano/lib/utility.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/vote_relay_index.hpp>

#include <algorithm>
#include <map>

std::vector<nano::vote_relay_index::query> nano::vote_relay_index::insert (std::shared_ptr<nano::transport::channel> const & channel, id_t id, std::vector<want> const & wants, bool include_non_final, std::chrono::steady_clock::time_point deadline)
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

		auto [it, inserted] = pending.insert ({ channel, id, want.hash, want.root, want.reps, include_non_final });
		debug_assert (inserted); // Caller deduplicates hashes within a request
		rows += inserted ? 1 : 0;
	}
	release_assert (rows > 0);
	requests.insert ({ channel, id, include_non_final, deadline, {}, rows });

	std::vector<query> result;
	result.reserve (queries.size ());
	for (auto & [rep, query] : queries)
	{
		result.push_back (std::move (query));
	}
	return result;
}

std::vector<nano::vote_relay_index::reply> nano::vote_relay_index::vote (std::shared_ptr<nano::vote> const & vote)
{
	if (pending.empty ())
	{
		return {};
	}

	auto const rep = vote->account;
	auto const final = vote->is_final ();

	// Requests this vote contributes to, with the number of pending rows it completed
	std::map<std::pair<std::shared_ptr<nano::transport::channel>, id_t>, std::size_t> touched;

	auto & by_hash = pending.get<tag_hash> ();
	for (auto const & hash : vote->hashes)
	{
		auto [begin, end] = by_hash.equal_range (hash);
		for (auto it = begin; it != end;)
		{
			bool const matches = (final || it->include_non_final) && std::find (it->reps.begin (), it->reps.end (), rep) != it->reps.end ();
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

	std::vector<reply> result;
	for (auto const & [key, completed] : touched)
	{
		auto const & [channel, id] = key;
		auto it = requests.get<tag_request> ().find (std::make_tuple (channel, id));
		release_assert (it != requests.get<tag_request> ().end ());

		requests.get<tag_request> ().modify (it, [&] (auto & request) {
			request.votes.push_back (vote);
			debug_assert (request.pending >= completed);
			request.pending -= completed;
		});

		if (it->pending == 0) // All rows answered, request is complete
		{
			result.push_back ({ it->channel, it->id, it->votes });
			requests.get<tag_request> ().erase (it);
		}
	}
	return result;
}

std::vector<nano::vote_relay_index::reply> nano::vote_relay_index::evict (std::chrono::steady_clock::time_point now)
{
	std::vector<reply> result;
	auto & by_deadline = requests.get<tag_deadline> ();
	while (!by_deadline.empty () && by_deadline.begin ()->deadline <= now)
	{
		auto const & request = *by_deadline.begin ();
		result.push_back ({ request.channel, request.id, request.votes });
		erase_pending (request.channel, request.id);
		by_deadline.erase (by_deadline.begin ());
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

std::optional<std::chrono::steady_clock::time_point> nano::vote_relay_index::next_deadline () const
{
	auto const & by_deadline = requests.get<tag_deadline> ();
	if (by_deadline.empty ())
	{
		return std::nullopt;
	}
	return by_deadline.begin ()->deadline;
}

std::size_t nano::vote_relay_index::size () const
{
	return requests.size ();
}

std::size_t nano::vote_relay_index::pending_size () const
{
	return pending.size ();
}

bool nano::vote_relay_index::empty () const
{
	return requests.empty ();
}
