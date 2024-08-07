#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/bootstrap/bootstrap_server.hpp>

namespace nano
{
class tomlconfig;

// TODO: This should be moved next to `account_sets` class
class account_sets_config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	std::size_t consideration_count{ 4 };
	std::size_t priorities_max{ 256 * 1024 };
	std::size_t blocking_max{ 256 * 1024 };
	std::chrono::milliseconds cooldown{ 1000 * 3 };
};

// TODO: This should be moved next to `bootstrap_ascending` class
class bootstrap_ascending_config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	bool enable{ true };
	bool enable_database_scan{ true };
	bool enable_dependency_walker{ true };

	// Maximum number of un-responded requests per channel
	std::size_t channel_limit{ 64 };
	std::size_t database_rate_limit{ 256 };
	std::size_t database_warmup_ratio{ 10 };
	std::size_t max_pull_count{ nano::bootstrap_server::max_blocks };
	std::chrono::milliseconds request_timeout{ 1000 * 5 };
	std::size_t throttle_coefficient{ 8 * 1024 };
	std::chrono::milliseconds throttle_wait{ 100 };
	std::size_t block_processor_threshold{ 1000 };
	std::size_t max_requests{ 1024 };

	nano::account_sets_config account_sets;
};
}
