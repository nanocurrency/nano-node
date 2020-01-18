#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace nano
{
class block_store;
class transaction;

class rep_weights
{
public:
	void representation_add (nano::account const & source_a, nano::uint128_t const & amount_a);
	nano::uint128_t representation_get (nano::account const & account_a);
	void representation_put (nano::account const & account_a, nano::uint128_union const & representation_a);
	std::unordered_map<nano::account, nano::uint128_t> get_rep_amounts ();

private:
	std::mutex mutex;
	std::unordered_map<nano::account, nano::uint128_t> rep_amounts;
	void put (nano::account const & account_a, nano::uint128_union const & representation_a);
	nano::uint128_t get (nano::account const & account_a);

	friend std::unique_ptr<container_info_component> collect_container_info (rep_weights &, const std::string &);
};

std::unique_ptr<container_info_component> collect_container_info (rep_weights &, const std::string &);
}
