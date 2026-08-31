#include <nano/lib/blocks.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/election_behavior.hpp>
#include <nano/node/election_status.hpp>

/*
 * confirmation_type
 */

std::string_view nano::to_string (nano::confirmation_type type)
{
	return nano::enum_to_string (type);
}

nano::stat::detail nano::to_stat_detail (nano::confirmation_type type)
{
	return nano::enum_convert<nano::stat::detail> (type);
}

/*
 * election_status
 */

void nano::election_status::operator() (nano::object_stream & obs) const
{
	obs.write ("winner", winner->hash ());
	obs.write ("tally_amount", tally.to_string_dec ());
	obs.write ("final_tally_amount", final_tally.to_string_dec ());
	obs.write ("election_end", nano::milliseconds_since_epoch (election_end));
	obs.write ("election_duration", election_duration.count ());
	obs.write ("confirmation_request_count", confirmation_request_count);
	obs.write ("vote_broadcast_count", vote_broadcast_count);
	obs.write ("block_count", block_count);
	obs.write ("voter_count", voter_count);
}

/*
 * election_extended_status
 */

void nano::election_extended_status::operator() (nano::object_stream & obs) const
{
	obs.write ("behavior", behavior);
	obs.write ("status", status);

	obs.write_range ("votes", votes, [] (auto const & entry, nano::object_stream & obs) {
		auto & [account, info] = entry;
		obs.write ("account", account);
		obs.write ("hash", info.hash);
		obs.write ("final", info.final ());
		obs.write ("timestamp", info.timestamp);
		obs.write ("arrival", info.arrival.time_since_epoch ().count ());
	});

	obs.write_range ("blocks", blocks, [] (auto const & entry) {
		auto [hash, block] = entry;
		return block;
	});

	obs.write_range ("tally", tally, [] (auto const & entry, nano::object_stream & obs) {
		auto & [key, block] = entry;
		obs.write ("hash", key.hash);
		obs.write ("amount", key.weight);
	});
}
