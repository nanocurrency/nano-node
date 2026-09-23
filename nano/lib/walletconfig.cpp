#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/lib/walletconfig.hpp>

nano::wallet_config::wallet_config ()
{
	nano::random_pool::generate_block (wallet.bytes.data (), wallet.bytes.size ());
	debug_assert (!wallet.is_zero ());
}

nano::error nano::wallet_config::parse (std::string const & wallet_a, std::string const & account_a)
{
	nano::error error;
	if (wallet.decode_hex (wallet_a))
	{
		error.set ("Invalid wallet id");
	}
	else if (account.decode_account (account_a))
	{
		error.set ("Invalid account format");
	}
	return error;
}

nano::error nano::wallet_config::serialize_toml (nano::tomlconfig & toml) const
{
	toml.put ("wallet", wallet.to_string (), "Wallet identifier\ntype:string,hex");
	toml.put ("account", account.to_account (), "Current wallet account\ntype:string,account");
	return toml.get_error ();
}

nano::error nano::wallet_config::deserialize_toml (nano::tomlconfig & toml)
{
	std::string wallet_l;
	std::string account_l;

	toml.get<std::string> ("wallet", wallet_l);
	toml.get<std::string> ("account", account_l);

	if (wallet.decode_hex (wallet_l))
	{
		toml.get_error ().set ("Invalid wallet id. Did you open a node daemon config?");
	}
	else if (account.decode_account (account_l))
	{
		toml.get_error ().set ("Invalid account");
	}

	return toml.get_error ();
}

nano::error nano::read_wallet_config (nano::wallet_config & config, std::filesystem::path const & data_path)
{
	return nano::read_config_file (config, nano::qtwallet_config_filename, data_path);
}

nano::error nano::write_wallet_config (nano::wallet_config const & config, std::filesystem::path const & data_path)
{
	nano::tomlconfig toml;
	config.serialize_toml (toml);
	// If missing, the file is created and permissions are set
	toml.write (nano::get_qtwallet_toml_config_path (data_path));
	return toml.get_error ();
}
