#pragma once

#include <nano/lib/numbers.hpp>

#include <boost/optional.hpp>

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace nano
{
class block;
class container_info_component;
class election;
class node;
class stats;
}
namespace nano::secure
{
class transaction;
}
namespace nano::scheduler
{
class bucket;
}

namespace nano::scheduler
{
class priority_config final
{
public:
	// nano::error deserialize (nano::tomlconfig & toml);
	// nano::error serialize (nano::tomlconfig & toml) const;

public:
	size_t depth{ 4096 };
};
class priority final
{
public:
	priority (nano::node &, nano::stats &);
	~priority ();

	/**
	 * Activates the first unconfirmed block of \p account
	 * @return Block that was evicted when the first unconfirmed block for \p account was activated
	 */
	std::shared_ptr<nano::block> activate (secure::transaction const & transaction, nano::account const & account);
	/**
	 * Activates the block \p block
	 * @return Block that was evicted when \p block was activated
	 */
	std::shared_ptr<nano::block> activate (secure::transaction const & transaction, std::shared_ptr<nano::block> const & block);
	/**
	 * Notify container when election has stopped to free space
	 */
	void election_stopped (std::shared_ptr<nano::election> election);

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name);

private: // Dependencies
	nano::node & node;
	nano::stats & stats;

private:
	void setup_buckets ();

	std::unordered_map<nano::block_hash, nano::scheduler::bucket *> tracking;
	std::map<nano::amount, std::unique_ptr<nano::scheduler::bucket>> buckets;
	mutable nano::mutex mutex;
};
}
