#pragma once

#include <celerix/lib/fwd.hpp>
#include <celerix/lib/locks.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/secure/fwd.hpp>
#include <celerix/store/fwd.hpp>

#include <cstdint>
#include <future>

namespace celerix
{
class epoch_upgrader final
{
public:
	epoch_upgrader (celerix::node &, celerix::ledger &, celerix::store::component &, celerix::network_params &, celerix::logger &);

	bool start (celerix::raw_key const & prv, celerix::epoch epoch, uint64_t count_limit, uint64_t threads);
	void stop ();

private: // Dependencies
	celerix::node & node;
	celerix::ledger & ledger;
	celerix::store::component & store;
	celerix::network_params & network_params;
	celerix::logger & logger;

private:
	void upgrade_impl (celerix::raw_key const & prv, celerix::epoch epoch, uint64_t count_limit, uint64_t threads);

	std::atomic<bool> stopped{ false };
	celerix::locked<std::future<void>> epoch_upgrading;
};
}
