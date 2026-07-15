#include <nano/messages/asc_pull.hpp>
#include <nano/node/bootstrap/bootstrap_context.hpp>
#include <nano/node/bootstrap/bootstrap_service.hpp>

#include <boost/property_tree/ptree.hpp>

nano::bootstrap_service::bootstrap_service (nano::node_config const & config_a, nano::node & node_a, nano::ledger & ledger_a, nano::ledger_notifications & ledger_notifications_a,
nano::block_processor & block_processor_a, nano::network & network_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ config_a },
	ledger{ ledger_a },
	ledger_notifications{ ledger_notifications_a },
	block_processor{ block_processor_a },
	network{ network_a },
	stats{ stats_a },
	logger{ logger_a },
	ctx_impl{ std::make_unique<nano::bootstrap::bootstrap_context> (config_a, node_a, ledger_a, ledger_notifications_a, block_processor_a, network_a, stats_a, logger_a) },
	ctx{ *ctx_impl }
{
}

nano::bootstrap_service::~bootstrap_service ()
{
}

void nano::bootstrap_service::start ()
{
	ctx.start ();
}

void nano::bootstrap_service::stop ()
{
	ctx.stop ();
}

void nano::bootstrap_service::process (nano::messages::asc_pull_ack const & message, std::shared_ptr<nano::transport::channel> const & channel)
{
	ctx.process (message, channel);
}

void nano::bootstrap_service::reset ()
{
	ctx.reset ();
}

void nano::bootstrap_service::prioritize (nano::account const & account)
{
	nano::lock_guard<nano::mutex> lock{ ctx.mutex };
	ctx.accounts.priority_set (account);
}

std::size_t nano::bootstrap_service::priority_size () const
{
	nano::lock_guard<nano::mutex> lock{ ctx.mutex };
	return ctx.accounts.priority_size ();
}

std::size_t nano::bootstrap_service::blocked_size () const
{
	nano::lock_guard<nano::mutex> lock{ ctx.mutex };
	return ctx.accounts.blocked_size ();
}

bool nano::bootstrap_service::prioritized (nano::account const & account) const
{
	nano::lock_guard<nano::mutex> lock{ ctx.mutex };
	return ctx.accounts.prioritized (account);
}

bool nano::bootstrap_service::blocked (nano::account const & account) const
{
	nano::lock_guard<nano::mutex> lock{ ctx.mutex };
	return ctx.accounts.blocked (account);
}

boost::property_tree::ptree nano::bootstrap_service::info () const
{
	nano::unique_lock<nano::mutex> lock{ ctx.mutex };

	// Copy the data under the lock, then serialize outside it
	auto [blocking, priorities] = ctx.accounts.info ();

	lock.unlock ();

	boost::property_tree::ptree result;

	// Priorities
	{
		boost::property_tree::ptree entries;
		for (auto const & entry : priorities)
		{
			boost::property_tree::ptree entry_l;
			entry_l.put ("account", entry.account.to_account ());
			entry_l.put ("priority", entry.priority);
			entries.push_back (std::make_pair ("", entry_l));
		}
		result.add_child ("priorities", entries);
	}
	// Blocking
	{
		boost::property_tree::ptree entries;
		for (auto const & entry : blocking)
		{
			boost::property_tree::ptree entry_l;
			entry_l.put ("account", entry.account.to_account ());
			entry_l.put ("dependency", entry.dependency.to_string ());
			entry_l.put ("dependency_account", entry.dependency_account.to_account ());
			entries.push_back (std::make_pair ("", entry_l));
		}
		result.add_child ("blocking", entries);
	}

	return result;
}

auto nano::bootstrap_service::status () const -> status_result
{
	nano::lock_guard<nano::mutex> lock{ ctx.mutex };
	return {
		.priorities = ctx.accounts.priority_size (),
		.blocking = ctx.accounts.blocked_size (),
	};
}

nano::container_info nano::bootstrap_service::container_info () const
{
	return ctx.container_info ();
}
