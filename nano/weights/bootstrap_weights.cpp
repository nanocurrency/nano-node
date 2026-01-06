#include <nano/lib/networks.hpp>
#include <nano/lib/utility.hpp>
#include <nano/weights/bootstrap_weights.hpp>
#include <nano/weights/bootstrap_weights_beta.hpp>
#include <nano/weights/bootstrap_weights_live.hpp>

nano::bootstrap_weights nano::get_bootstrap_weights (nano::network_type type)
{
	std::vector<std::pair<std::string, std::string>> preconfigured;
	uint64_t max_blocks{};

	switch (type)
	{
		case nano::network_type::nano_live_network:
			preconfigured = nano::weights::preconfigured_weights_live;
			max_blocks = nano::weights::max_blocks_live;
			break;
		case nano::network_type::nano_beta_network:
			preconfigured = nano::weights::preconfigured_weights_beta;
			max_blocks = nano::weights::max_blocks_beta;
			break;
		case nano::network_type::nano_dev_network:
		case nano::network_type::nano_test_network:
		case nano::network_type::invalid:
			return {};
	}

	nano::bootstrap_weights result{};
	result.max_blocks = max_blocks;
	for (auto const & [account_str, weight_str] : preconfigured)
	{
		nano::account account;
		bool error = account.decode_account (account_str);
		release_assert (!error, "invalid bootstrap weight account");

		result.representatives[account] = nano::uint128_t{ weight_str };
	}
	return result;
}
