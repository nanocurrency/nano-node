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
	toml.get ("cooldown", cooldown);

	return toml.get_error ();
}

nano::error nano::account_sets_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("consideration_count", consideration_count, "Limit the number of account candidates to consider and also the number of iterations.\ntype:uint64");
	toml.put ("priorities_max", priorities_max, "Cutoff size limit for the priority list.\ntype:uint64");
	toml.put ("blocking_max", blocking_max, "Cutoff size limit for the blocked accounts from the priority list.\ntype:uint64");
	toml.put ("cooldown", cooldown, "Waiting time for an account to become available.\ntype:milliseconds");

	return toml.get_error ();
}

/*
 * bootstrap_ascending_config
 */
nano::error nano::bootstrap_ascending_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("requests_limit", requests_limit);
	toml.get ("database_requests_limit", database_requests_limit);
	toml.get ("pull_count", pull_count);
	toml.get ("timeout", timeout);
	toml.get ("throttle_coefficient", throttle_coefficient);
	toml.get ("throttle_wait", throttle_wait);
	toml.get ("block_wait_count", block_wait_count);

	if (toml.has_key ("account_sets"))
	{
		auto config_l = toml.get_required_child ("account_sets");
		account_sets.deserialize (config_l);
	}

	return toml.get_error ();
}

nano::error nano::bootstrap_ascending_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("requests_limit", requests_limit, "Request limit to ascending bootstrap after which requests will be dropped.\nNote: changing to unlimited (0) is not recommended.\ntype:uint64");
	toml.put ("database_requests_limit", database_requests_limit, "Request limit for accounts from database after which requests will be dropped.\nNote: changing to unlimited (0) is not recommended as this operation competes for resources on querying the database.\ntype:uint64");
	toml.put ("pull_count", pull_count, "Number of requested blocks for ascending bootstrap request.\ntype:uint64");
	toml.put ("timeout", timeout, "Timeout in milliseconds for incoming ascending bootstrap messages to be processed.\ntype:milliseconds");
	toml.put ("throttle_coefficient", throttle_coefficient, "Scales the number of samples to track for bootstrap throttling.\ntype:uint64");
	toml.put ("throttle_wait", throttle_wait, "Length of time to wait between requests when throttled.\ntype:milliseconds");
	toml.put ("block_wait_count", block_wait_count, "Asending bootstrap will wait while block processor has more than this many blocks queued.\ntype:uint64");

	nano::tomlconfig account_sets_l;
	account_sets.serialize (account_sets_l);
	toml.put_child ("account_sets", account_sets_l);

	return toml.get_error ();
}
