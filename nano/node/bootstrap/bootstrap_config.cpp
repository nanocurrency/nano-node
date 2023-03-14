#include <nano/lib/tomlconfig.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>

nano::error nano::bootstrap_ascending_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("requests_limit", requests_limit);
	toml.get ("database_requests_limit", database_requests_limit);
	toml.get ("pull_count", pull_count);
	toml.get ("timeout", timeout);

	return toml.get_error ();
}

nano::error nano::bootstrap_ascending_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("requests_limit", requests_limit, "Request limit to ascending bootstrap after which requests will be dropped.\nNote: changing to unlimited (0) is not recommended.\ntype:uint64");
	toml.put ("database_requests_limit", database_requests_limit, "Request limit for accounts from database after which requests will be dropped.\nNote: changing to unlimited (0) is not recommended as this operation competes for resources on querying the database.\ntype:uint64");
	toml.put ("pull_count", pull_count, "Number of requested blocks for ascending bootstrap request.\ntype:uint64");
	toml.put ("timeout", timeout, "Timeout in milliseconds for incoming ascending bootstrap messages to be processed.\ntype:milliseconds");

	return toml.get_error ();
}
