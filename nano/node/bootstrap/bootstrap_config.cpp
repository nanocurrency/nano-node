#include <nano/lib/tomlconfig.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>

/*
 * account_sets_config
 */

nano::error nano::account_sets_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("consideration_count", consideration_count);
	toml.get ("priorities_max", priorities_max);
	toml.get ("blocking_max", blocking_max);
	toml.get_duration ("cooldown", cooldown);
	toml.get_duration ("blocking_decay", blocking_decay);

	return toml.get_error ();
}

nano::error nano::account_sets_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("consideration_count", consideration_count, "Limit the number of account candidates to consider and also the number of iterations.\ntype:uint64");
	toml.put ("priorities_max", priorities_max, "Cutoff size limit for the priority list.\ntype:uint64");
	toml.put ("blocking_max", blocking_max, "Cutoff size limit for the blocked accounts from the priority list.\ntype:uint64");
	toml.put ("cooldown", cooldown.count (), "Waiting time for an account to become available.\ntype:milliseconds");
	toml.put ("blocking_decay", blocking_decay.count (), "Time to wait before removing an account from the blocked list.\ntype:seconds");

	return toml.get_error ();
}

/*
 * frontier_scan_config
 */

nano::error nano::frontier_scan_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("head_parallelism", head_parallelism);
	toml.get ("consideration_count", consideration_count);
	toml.get ("candidates", candidates);
	toml.get_duration ("cooldown", cooldown);
	toml.get ("max_pending", max_pending);

	return toml.get_error ();
}

nano::error nano::frontier_scan_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("head_parallelism", head_parallelism, "Number of accounts to process in parallel during frontier scan.\ntype:uint64");
	toml.put ("consideration_count", consideration_count, "Number of account candidates to consider for frontier scan.\ntype:uint64");
	toml.put ("candidates", candidates, "Maximum number of candidates for frontier scan.\ntype:uint64");
	toml.put ("cooldown", cooldown.count (), "Cooldown period between frontier scan operations.\ntype:milliseconds");
	toml.put ("max_pending", max_pending, "Maximum number of pending requests during frontier scan.\ntype:uint64");

	return toml.get_error ();
}

/*
 * bootstrap_config
 */

nano::error nano::bootstrap_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("enable_priorities", enable_priorities);
	toml.get ("enable_database_scan", enable_database_scan);
	toml.get ("enable_dependency_walker", enable_dependency_walker);
	toml.get ("enable_frontier_scan", enable_frontier_scan);

	toml.get ("channel_limit", channel_limit);
	toml.get ("rate_limit", rate_limit);
	toml.get ("database_rate_limit", database_rate_limit);
	toml.get ("frontier_rate_limit", frontier_rate_limit);
	toml.get ("database_warmup_ratio", database_warmup_ratio);
	toml.get ("max_pull_count", max_pull_count);
	toml.get_duration ("request_timeout", request_timeout);
	toml.get ("throttle_coefficient", throttle_coefficient);
	toml.get_duration ("throttle_wait", throttle_wait);
	toml.get ("block_processor_threshold", block_processor_threshold);
	toml.get ("max_requests", max_requests);
	toml.get ("optimistic_request_percentage", optimistic_request_percentage);

	if (toml.has_key ("account_sets"))
	{
		auto config_l = toml.get_required_child ("account_sets");
		account_sets.deserialize (config_l);
	}

	if (toml.has_key ("frontier_scan"))
	{
		auto config_l = toml.get_required_child ("frontier_scan");
		frontier_scan.deserialize (config_l);
	}

	return toml.get_error ();
}

nano::error nano::bootstrap_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable the bootstrap. Disabling it is not recommended and will prevent the node from syncing.\ntype:bool");
	toml.put ("enable_priorities", enable_priorities, "Enable or disable the 'priorities` strategy for the ascending bootstrap.\ntype:bool");
	toml.put ("enable_database_scan", enable_database_scan, "Enable or disable the 'database scan` strategy for the ascending bootstrap.\ntype:bool");
	toml.put ("enable_dependency_walker", enable_dependency_walker, "Enable or disable the 'dependency walker` strategy for the ascending bootstrap.\ntype:bool");
	toml.put ("enable_frontier_scan", enable_frontier_scan, "Enable or disable the 'frontier scan` strategy for the ascending bootstrap.\ntype:bool");

	toml.put ("channel_limit", channel_limit, "Maximum number of un-responded requests per channel.\nNote: changing to unlimited (0) is not recommended.\ntype:uint64");
	toml.put ("rate_limit", rate_limit, "Rate limit on requests.\nNote: changing to unlimited (0) is not recommended as this operation competes for resources with realtime traffic.\ntype:uint64");
	toml.put ("database_rate_limit", database_rate_limit, "Rate limit on scanning accounts and pending entries from database.\nNote: changing to unlimited (0) is not recommended as this operation competes for resources on querying the database.\ntype:uint64");
	toml.put ("frontier_rate_limit", frontier_rate_limit, "Rate limit on scanning frontiers.\nNote: changing to unlimited (0) is not recommended as this operation competes for resources on querying the network.\ntype:uint64");
	toml.put ("database_warmup_ratio", database_warmup_ratio, "Ratio of the database rate limit to use for the initial warmup.\ntype:uint64");
	toml.put ("max_pull_count", max_pull_count, "Maximum number of requested blocks for bootstrap request.\ntype:uint64");
	toml.put ("request_timeout", request_timeout.count (), "Timeout in milliseconds for incoming bootstrap messages to be processed.\ntype:milliseconds");
	toml.put ("throttle_coefficient", throttle_coefficient, "Scales the number of samples to track for bootstrap throttling.\ntype:uint64");
	toml.put ("throttle_wait", throttle_wait.count (), "Length of time to wait between requests when throttled.\ntype:milliseconds");
	toml.put ("block_processor_threshold", block_processor_threshold, "Bootstrap will wait while block processor has more than this many blocks queued.\ntype:uint64");
	toml.put ("max_requests", max_requests, "Maximum total number of in flight requests.\ntype:uint64");
	toml.put ("optimistic_request_percentage", optimistic_request_percentage, "Percentage of requests that will be optimistic. Optimistic requests start from the (possibly unconfirmed) account frontier and are vulnerable to bootstrap poisoning. Safe requests start from the confirmed frontier and given enough time will eventually resolve forks.\ntype:uint64");

	nano::tomlconfig account_sets_l;
	account_sets.serialize (account_sets_l);
	toml.put_child ("account_sets", account_sets_l);

	nano::tomlconfig frontier_scan_l;
	frontier_scan.serialize (frontier_scan_l);
	toml.put_child ("frontier_scan", frontier_scan_l);

	return toml.get_error ();
}