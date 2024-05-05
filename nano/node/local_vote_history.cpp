#include <nano/node/local_vote_history.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/vote.hpp>

bool nano::local_vote_history::consistency_check (nano::root const & root_a) const
{
	auto & history_by_root (history.get<tag_root> ());
	auto const range (history_by_root.equal_range (root_a));
	// All cached votes for a root must be for the same hash, this is actively enforced in local_vote_history::add
	auto consistent_same = std::all_of (range.first, range.second, [hash = range.first->hash] (auto const & info_a) { return info_a.hash == hash; });
	std::vector<nano::account> accounts;
	std::transform (range.first, range.second, std::back_inserter (accounts), [] (auto const & info_a) { return info_a.vote->account; });
	std::sort (accounts.begin (), accounts.end ());
	// All cached votes must be unique by account, this is actively enforced in local_vote_history::add
	auto consistent_unique = accounts.size () == std::unique (accounts.begin (), accounts.end ()) - accounts.begin ();
	auto result = consistent_same && consistent_unique;
	debug_assert (result);
	return result;
}

void nano::local_vote_history::add (nano::root const & root_a, nano::block_hash const & hash_a, std::shared_ptr<nano::vote> const & vote_a)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
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

void nano::local_vote_history::erase (nano::root const & root_a)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	auto & history_by_root (history.get<tag_root> ());
	auto range (history_by_root.equal_range (root_a));
	history_by_root.erase (range.first, range.second);
}

std::vector<std::shared_ptr<nano::vote>> nano::local_vote_history::votes (nano::root const & root_a) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	std::vector<std::shared_ptr<nano::vote>> result;
	auto range (history.get<tag_root> ().equal_range (root_a));
	std::transform (range.first, range.second, std::back_inserter (result), [] (auto const & entry) { return entry.vote; });
	return result;
}

std::vector<std::shared_ptr<nano::vote>> nano::local_vote_history::votes (nano::root const & root_a, nano::block_hash const & hash_a, bool const is_final_a) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	std::vector<std::shared_ptr<nano::vote>> result;
	auto range (history.get<tag_root> ().equal_range (root_a));
	// clang-format off
	nano::transform_if (range.first, range.second, std::back_inserter (result),
		[&hash_a, is_final_a](auto const & entry) { return entry.hash == hash_a && (!is_final_a || entry.vote->timestamp () == std::numeric_limits<uint64_t>::max ()); },
		[](auto const & entry) { return entry.vote; });
	// clang-format on
	return result;
}

bool nano::local_vote_history::exists (nano::root const & root_a) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return history.get<tag_root> ().find (root_a) != history.get<tag_root> ().end ();
}

void nano::local_vote_history::clean ()
{
	debug_assert (constants.max_cache > 0);
	auto & history_by_sequence (history.get<tag_sequence> ());
	while (history_by_sequence.size () > constants.max_cache)
	{
		history_by_sequence.erase (history_by_sequence.begin ());
	}
}

std::size_t nano::local_vote_history::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return history.size ();
}

std::unique_ptr<nano::container_info_component> nano::local_vote_history::collect_container_info (std::string const & name) const
{
	std::size_t history_count = size ();
	auto sizeof_element = sizeof (decltype (history)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	/* This does not currently loop over each element inside the cache to get the sizes of the votes inside history*/
	composite->add_component (std::make_unique<container_info_leaf> (container_info_entry{ "history", history_count, sizeof_element }));
	return composite;
}
