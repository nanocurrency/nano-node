#include <nano/lib/threading.hpp>
#include <nano/node/backlog_population.hpp>
#include <nano/node/election_scheduler.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/secure/store.hpp>

nano::backlog_population::backlog_population (nano::node_config & config_a, nano::store & store_a, nano::election_scheduler & scheduler_a) :
	config{ config_a },
	store_m{ store_a },
	scheduler{ scheduler_a }
{
}

nano::backlog_population::~backlog_population ()
{
	stop ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void nano::backlog_population::start ()
{
	if (!thread.joinable ())
	{
		thread = std::thread{ [this] () { run (); } };
	}
}

void nano::backlog_population::stop ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	stopped = true;
	notify ();
}

void nano::backlog_population::trigger ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	triggered = true;
	notify ();
}

void nano::backlog_population::notify ()
{
	condition.notify_all ();
}

bool nano::backlog_population::predicate () const
{
	return triggered;
}

void nano::backlog_population::run ()
{
	nano::thread_role::set (nano::thread_role::name::backlog_population);
	const auto delay = config.network_params.network.is_dev_network () ? std::chrono::seconds{ 1 } : std::chrono::duration_cast<std::chrono::seconds> (std::chrono::minutes{ 5 });
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		if (predicate () || (config.frontiers_confirmation != nano::frontiers_confirmation_mode::disabled))
		{
			triggered = false;
			lock.unlock ();
			populate_backlog ();
			lock.lock ();
		}

		condition.wait_for (lock, delay, [this] () {
			return stopped || predicate ();
		});
	}
}

void nano::backlog_population::populate_backlog ()
{
	auto done = false;
	uint64_t const chunk_size = 65536;
	nano::account next = 0;
	uint64_t total = 0;
	while (!stopped && !done)
	{
		auto transaction = store_m.tx_begin_read ();
		auto count = 0;
		auto i = store_m.account.begin (transaction, next);
		const auto end = store_m.account.end ();
		for (; !stopped && i != end && count < chunk_size; ++i, ++count, ++total)
		{
			auto const & account = i->first;
			scheduler.activate (account, transaction);
			next = account.number () + 1;
		}
		done = store_m.account.begin (transaction, next) == end;
	}
}
