#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/numbers.hpp>

#include <string>

namespace nano
{
class tomlconfig;

/** Configuration options for the Qt wallet */
class wallet_config final
{
public:
	wallet_config ();
	/** Update this instance by parsing the given wallet and account */
	nano::error parse (std::string const & wallet_a, std::string const & account_a);
	nano::error serialize_toml (nano::tomlconfig & toml_a) const;
	nano::error deserialize_toml (nano::tomlconfig & toml_a);
	nano::wallet_id wallet;
	nano::account account{ static_cast<std::uint64_t> (0) };
};
}
