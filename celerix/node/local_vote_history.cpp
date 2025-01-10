#include <celerix/node/local_vote_history.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/vote.hpp>

bool celerix::local_vote_history::consistency_check (celerix::root const & root_a) const
{
	auto & history_by_root (history.get<tag_root> ());
	auto const range (history_by_root.equal_range (root_a));
	// All cached votes for a root must be for the same hash, this is actively enforced in local_vote_history::add
	auto consistent_same = std::all_of (range.first, range.second, [hash = range.first->hash] (auto const & info_a) { return info_a.hash == hash; });
	std::vector<celerix::account> accounts;
	std::transform (range.first, range.second, std::back_inserter (accounts), [] (auto const & info_a) { return info_a.vote->account; });
	std::sort (accounts.begin (), accounts.end ());
	// All cached votes must be unique by account, this is actively enforced in local_vote_history::add
	auto consistent_unique = accounts.size () == std::unique (accounts.begin (), accounts.end ()) - accounts.begin ();
	auto result = consistent_same && consistent_unique;
	debug_assert (result);
	return result;
}

void celerix::local_vote_history::add (celerix::root const & root_a, celerix::block_hash const & hash_a, std::shared_ptr<celerix::vote> const & vote_a)
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	clean ();
	auto add_vote (true);
	auto & history_by_root (history.get<tag_root> ());
	// Erase any vote that is not for this hash, or duplicate by account, and if new timestamp is higher
	auto range (history_by_root.equal_range (root_a));
	for (auto i (range.first); i != range.second;)
	{
		if (i->hash != hash_a || (vote_a->account == i->vote->account && i->vote->timestamp () <= vote_a->timestamp ()))
		{
			i = history_by_root.erase (i);
		}
		else if (vote_a->account == i->vote->account && i->vote->timestamp () > vote_a->timestamp ())
		{
			add_vote = false;
			++i;
		}
		else
		{
			++i;
		}
	}
	// Do not add new vote to cache if representative account is same and timestamp is lower
	if (add_vote)
	{
		auto result (history_by_root.emplace (root_a, hash_a, vote_a));
		(void)result;
		debug_assert (result.second);
	}
	debug_assert (consistency_check (root_a));
}

void celerix::local_vote_history::erase (celerix::root const & root_a)
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	auto & history_by_root (history.get<tag_root> ());
	auto range (history_by_root.equal_range (root_a));
	history_by_root.erase (range.first, range.second);
}

std::vector<std::shared_ptr<celerix::vote>> celerix::local_vote_history::votes (celerix::root const & root_a) const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	std::vector<std::shared_ptr<celerix::vote>> result;
	auto range (history.get<tag_root> ().equal_range (root_a));
	std::transform (range.first, range.second, std::back_inserter (result), [] (auto const & entry) { return entry.vote; });
	return result;
}

std::vector<std::shared_ptr<celerix::vote>> celerix::local_vote_history::votes (celerix::root const & root_a, celerix::block_hash const & hash_a, bool const is_final_a) const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	std::vector<std::shared_ptr<celerix::vote>> result;
	auto range (history.get<tag_root> ().equal_range (root_a));
	// clang-format off
	celerix::transform_if (range.first, range.second, std::back_inserter (result),
		[&hash_a, is_final_a](auto const & entry) { return entry.hash == hash_a && (!is_final_a || entry.vote->timestamp () == std::numeric_limits<uint64_t>::max ()); },
		[](auto const & entry) { return entry.vote; });
	// clang-format on
	return result;
}

bool celerix::local_vote_history::exists (celerix::root const & root_a) const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return history.get<tag_root> ().find (root_a) != history.get<tag_root> ().end ();
}

void celerix::local_vote_history::clean ()
{
	debug_assert (constants.max_cache > 0);
	auto & history_by_sequence (history.get<tag_sequence> ());
	while (history_by_sequence.size () > constants.max_cache)
	{
		history_by_sequence.erase (history_by_sequence.begin ());
	}
}

std::size_t celerix::local_vote_history::size () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return history.size ();
}

celerix::container_info celerix::local_vote_history::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("history", history);
	return info;
}