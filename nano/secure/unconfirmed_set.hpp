#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/secure/account_info.hpp>
#include <nano/secure/block_delta.hpp>
#include <nano/secure/pending_info.hpp>

#include <deque>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace nano
{
class block;
}

namespace nano
{
class unconfirmed_set
{
public:
	std::unordered_map<nano::block_hash, block_delta> block;
	std::unordered_map<nano::account, nano::account_info> account;
	std::map<nano::pending_key, nano::pending_info> receivable;
	std::unordered_set<nano::pending_key> received;
	std::unordered_map<nano::block_hash, nano::block_hash> successor;
	std::unordered_map<nano::account, nano::amount> weight;
	bool receivable_any (nano::account const & account) const;
	void weight_add (nano::account const & account, nano::amount const & amount, nano::amount const & base);
};
}
