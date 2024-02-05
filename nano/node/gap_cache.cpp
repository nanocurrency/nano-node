#include <nano/node/gap_cache.hpp>
#include <nano/node/node.hpp>

#include <boost/format.hpp>

nano::gap_cache::gap_cache (nano::node & node_a, nano::block_processor & block_processor_a) :
	node (node_a),
	block_processor (block_processor_a)
{
	block_processor.processed.add ([this] (auto const & result, auto const & block) {
		switch (result.code)
		{
			case nano::process_result::gap_previous:
			case nano::process_result::gap_source:
				add (block->hash ());
				break;
			default:
				break;
		}
	});
}

void nano::gap_cache::add (nano::block_hash const & hash_a, std::chrono::steady_clock::time_point time_point_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto existing (blocks.get<tag_hash> ().find (hash_a));
	if (existing != blocks.get<tag_hash> ().end ())
	{
		blocks.get<tag_hash> ().modify (existing, [time_point_a] (nano::gap_information & info) {
			info.arrival = time_point_a;
		});
	}
	else
	{
		blocks.get<tag_arrival> ().emplace (nano::gap_information{ time_point_a, hash_a, std::vector<nano::account> () });
		if (blocks.get<tag_arrival> ().size () > max)
		{
			blocks.get<tag_arrival> ().erase (blocks.get<tag_arrival> ().begin ());
		}
	}
}

void nano::gap_cache::erase (nano::block_hash const & hash_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	blocks.get<tag_hash> ().erase (hash_a);
}

void nano::gap_cache::vote (std::shared_ptr<nano::vote> const & vote_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	for (auto const & hash : vote_a->hashes)
	{
		auto & gap_blocks_by_hash (blocks.get<tag_hash> ());
		auto existing (gap_blocks_by_hash.find (hash));
		if (existing != gap_blocks_by_hash.end () && !existing->bootstrap_started)
		{
			auto is_new (false);
			gap_blocks_by_hash.modify (existing, [&is_new, &vote_a] (nano::gap_information & info) {
				auto it = std::find (info.voters.begin (), info.voters.end (), vote_a->account);
				is_new = (it == info.voters.end ());
				if (is_new)
				{
					info.voters.push_back (vote_a->account);
				}
			});

			if (is_new)
			{
				if (bootstrap_check (existing->voters, hash))
				{
					gap_blocks_by_hash.modify (existing, [] (nano::gap_information & info) {
						info.bootstrap_started = true;
					});
				}
			}
		}
	}
}

bool nano::gap_cache::bootstrap_check (std::vector<nano::account> const & voters_a, nano::block_hash const & hash_a)
{
	nano::uint128_t tally;
	for (auto const & voter : voters_a)
	{
		tally += node.ledger.weight (voter);
	}
	bool start_bootstrap (false);
	if (!node.flags.disable_lazy_bootstrap)
	{
		if (tally >= node.online_reps.delta ())
		{
			start_bootstrap = true;
		}
	}
	else if (!node.flags.disable_legacy_bootstrap && tally > bootstrap_threshold ())
	{
		start_bootstrap = true;
	}
	if (start_bootstrap && !node.ledger.block_or_pruned_exists (hash_a))
	{
		bootstrap_start (hash_a);
	}
	return start_bootstrap;
}

void nano::gap_cache::bootstrap_start (nano::block_hash const & hash_a)
{
	auto node_l (node.shared ());
	node.workers.add_timed_task (std::chrono::steady_clock::now () + node.network_params.bootstrap.gap_cache_bootstrap_start_interval, [node_l, hash_a] () {
		if (!node_l->ledger.block_or_pruned_exists (hash_a))
		{
			if (!node_l->bootstrap_initiator.in_progress ())
			{
				node_l->logger.debug (nano::log::type::gap_cache, "Block {} has enough votes to warrant lazy bootstrapping it", hash_a.to_string ());
			}
			if (!node_l->flags.disable_lazy_bootstrap)
			{
				node_l->bootstrap_initiator.bootstrap_lazy (hash_a);
			}
			else if (!node_l->flags.disable_legacy_bootstrap)
			{
				node_l->bootstrap_initiator.bootstrap ();
			}
		}
	});
}

nano::uint128_t nano::gap_cache::bootstrap_threshold ()
{
	auto result ((node.online_reps.trended () / 256) * node.config.bootstrap_fraction_numerator);
	return result;
}

std::size_t nano::gap_cache::size ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return blocks.size ();
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (gap_cache & gap_cache, std::string const & name)
{
	auto count = gap_cache.size ();
	auto sizeof_element = sizeof (decltype (gap_cache.blocks)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "blocks", count, sizeof_element }));
	return composite;
}
