#include <nano/node/backlog_population.hpp>
#include <nano/node/node.hpp>

nano::backlog_population::backlog_population (nano::node & node_a) :
	node{ node_a }
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
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		if (predicate () || (node.config.frontiers_confirmation != nano::frontiers_confirmation_mode::disabled))
		{
			lock.unlock ();
			populate_backlog ();
			lock.lock ();
			triggered = false;
		}

		auto delay = node.config.network_params.network.is_dev_network () ? std::chrono::seconds{ 1 } : std::chrono::duration_cast<std::chrono::seconds> (std::chrono::minutes{ 5 });

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
		auto transaction = node.store.tx_begin_read ();
		auto count = 0;
		for (auto i = node.store.account.begin (transaction, next), n = node.store.account.end (); !stopped && i != n && count < chunk_size; ++i, ++count, ++total)
		{
			auto const & account = i->first;
			node.scheduler.activate (account, transaction);
			next = account.number () + 1;
		}
		done = node.store.account.begin (transaction, next) == node.store.account.end ();
	}
}