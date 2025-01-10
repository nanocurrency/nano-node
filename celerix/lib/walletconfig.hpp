#pragma once

#include <celerix/lib/errors.hpp>
#include <celerix/lib/numbers.hpp>

#include <string>

namespace celerix
{
class tomlconfig;

/** Configuration options for the Qt wallet */
class wallet_config final
{
public:
	wallet_config ();
	/** Update this instance by parsing the given wallet and account */
	celerix::error parse (std::string const & wallet_a, std::string const & account_a);
	celerix::error serialize_toml (celerix::tomlconfig & toml_a) const;
	celerix::error deserialize_toml (celerix::tomlconfig & toml_a);
	celerix::wallet_id wallet;
	celerix::account account{};
};
}
