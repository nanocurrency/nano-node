#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/numbers.hpp>

#include <filesystem>
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
	nano::account account{};
};

/** Reads the Qt wallet config from \p data_path into \p config */
nano::error read_wallet_config (nano::wallet_config & config, std::filesystem::path const & data_path);
/** Writes \p config as the Qt wallet config in \p data_path, replacing any previous content */
nano::error write_wallet_config (nano::wallet_config const & config, std::filesystem::path const & data_path);
}
