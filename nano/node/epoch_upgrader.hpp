#pragma once

#include <nano/lib/epoch.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>

#include <future>

namespace nano
{
class node;
class ledger;
class store;
class network_params;
class logger_mt;

class epoch_upgrader final
{
public:
	epoch_upgrader (nano::node &, nano::ledger &, nano::store &, nano::network_params &, nano::logger_mt &);

	bool start (nano::raw_key const & prv, nano::epoch epoch, uint64_t count_limit, uint64_t threads);
	void stop ();

private: // Dependencies
	nano::node & node;
	nano::ledger & ledger;
	nano::store & store;
	nano::network_params & network_params;
	nano::logger_mt & logger;

private:
	void upgrade_impl (nano::raw_key const & prv, nano::epoch epoch, uint64_t count_limit, uint64_t threads);

	std::atomic<bool> stopped{ false };
	nano::locked<std::future<void>> epoch_upgrading;
};
}