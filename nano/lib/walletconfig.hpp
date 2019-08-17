#pragma once

#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/numbers.hpp>

#include <string>

namespace nano
{
class jsonconfig;
class tomlconfig;

/** Configuration options for the Qt wallet */
class wallet_config final
{
public:
	wallet_config ();
	/** Update this instance by parsing the given wallet and account */
	nano::error parse (std::string wallet_a, std::string account_a);
	nano::error serialize_toml (nano::tomlconfig & toml_a) const;
	nano::error deserialize_toml (nano::tomlconfig & toml_a);
	nano::uint256_union wallet;
	nano::account account{ 0 };
};
}
