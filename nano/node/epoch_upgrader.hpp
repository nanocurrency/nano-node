#pragma once

#include <nano/lib/epoch.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>

#include <future>

namespace nano
{
class node;
class ledger;
namespace store
{
	class component;
}
class network_params;

class epoch_upgrader final
{
public:
	epoch_upgrader (nano::node &, nano::ledger &, nano::store::component &, nano::network_params &, nano::nlogger &);

	bool start (nano::raw_key const & prv, nano::epoch epoch, uint64_t count_limit, uint64_t threads);
	void stop ();

private: // Dependencies
	nano::node & node;
	nano::ledger & ledger;
	nano::store::component & store;
	nano::network_params & network_params;
	nano::nlogger & nlogger;

private:
	void upgrade_impl (nano::raw_key const & prv, nano::epoch epoch, uint64_t count_limit, uint64_t threads);

	std::atomic<bool> stopped{ false };
	nano::locked<std::future<void>> epoch_upgrading;
};
}
