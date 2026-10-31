#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/node/transport/formatting.hpp>
#include <nano/node/vote_replier.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/vote.hpp>
#include <nano/secure/voting_policy.hpp>

nano::vote_replier::vote_replier (vote_replier_config const & config_a, nano::voting_policy & policy_a, nano::ledger & ledger_a, nano::wallet::wallets & wallets_a, nano::network_constants const & network_constants_a, nano::stats & stats_a, nano::logger & logger_a, bool enable_voting) :
	config{ config_a },
	policy{ policy_a },
	ledger{ ledger_a },
	wallets{ wallets_a },
	network_constants{ network_constants_a },
	stats{ stats_a },
	logger{ logger_a },
	enabled{ enable_voting }
{
	queue.max_size_query = [this] (auto const & origin) {
		return config.channel_limit;
	};
	queue.priority_query = [] (auto const & origin) {
		return 1;
	};
}

nano::vote_replier::~vote_replier ()
{
	debug_assert (threads.empty ());
}

void nano::vote_replier::start ()
{
	debug_assert (threads.empty ());

	if (!enabled)
	{
		return;
	}

	for (auto i = 0; i < config.threads; ++i)
	{
		threads.emplace_back ([this] () {
			nano::thread_role::set (nano::thread_role::name::vote_replier);
			run ();
		});
	}
}

void nano::vote_replier::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	for (auto & thread : threads)
	{
		if (thread.joinable ())
		{
			thread.join ();
		}
	}
	threads.clear ();
}

std::size_t nano::vote_replier::size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return queue.size ();
}

bool nano::vote_replier::empty () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return queue.empty ();
}

bool nano::vote_replier::request (request_type const & request, std::shared_ptr<nano::transport::channel> const & channel)
{
	release_assert (channel != nullptr);
	debug_assert (!request.empty ());

	if (!enabled)
	{
		return false;
	}

	if (request.size () > nano::messages::confirm_req::max_hashes)
	{
		stats.inc (nano::stat::type::vote_replier, nano::stat::detail::oversize);
		return false;
	}

	bool added = false;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		added = queue.push ({ request, channel }, { nano::no_value{}, channel });
	}
	if (added)
	{
		stats.inc (nano::stat::type::vote_replier, nano::stat::detail::request);
		stats.add (nano::stat::type::vote_replier, nano::stat::detail::request_hashes, request.size ());

		condition.notify_one ();
	}
	else
	{
		stats.inc (nano::stat::type::vote_replier, nano::stat::detail::overfill);
		stats.add (nano::stat::type::vote_replier, nano::stat::detail::overfill_hashes, request.size ());
	}

	return added;
}

void nano::vote_replier::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::vote_replier, nano::stat::detail::loop);

		if (!queue.empty ())
		{
			if (queue.size () > nano::queue_warning_threshold () && log_interval.elapse (15s))
			{
				logger.info (nano::log::type::vote_replier, "{} requests in processing queue", queue.size ());
			}

			auto batch = next_batch ();
			if (!batch.empty ())
			{
				lock.unlock ();
				run_batch (batch);
				lock.lock ();
			}
			else
			{
				// Every queued request waits on a channel with no room for a reply, give the sockets time to drain before trying again
				stats.inc (nano::stat::type::vote_replier, nano::stat::detail::channel_full);
				condition.wait_for (lock, 100ms); // A stop or a new request ends the wait early
			}
		}
		else
		{
			condition.wait (lock, [&] { return stopped || !queue.empty (); });
		}
	}
}

auto nano::vote_replier::next_batch () -> std::deque<queue_type::value_type>
{
	debug_assert (!mutex.try_lock ());
	debug_assert (config.batch_size > 0);

	// A dead channel counts as ready so its requests come out and get discarded instead of lingering
	return queue.next_ready_batch (config.batch_size, [] (auto const & origin) {
		return !origin.channel->alive () || !origin.channel->max (nano::transport::traffic_type::vote_reply);
	});
}

void nano::vote_replier::run_batch (std::deque<queue_type::value_type> const & batch)
{
	debug_assert (!batch.empty ());

	auto transaction = ledger.tx_begin_read ();

	for (auto const & [value, origin] : batch)
	{
		auto const & [request, channel] = value;

		transaction.refresh_if_needed ();

		if (channel->alive ())
		{
			process (transaction, request, channel);
		}
		else
		{
			stats.inc (nano::stat::type::vote_replier, nano::stat::detail::channel_dead);
		}
	}
}

void nano::vote_replier::process (nano::secure::transaction const & transaction, request_type const & request, std::shared_ptr<nano::transport::channel> const & channel)
{
	debug_assert (request.size () <= nano::messages::confirm_req::max_hashes);

	std::vector<nano::vote_permit> permits;
	permits.reserve (request.size ());

	for (auto const & [hash, root] : request)
	{
		auto block = ledger.block_find (transaction, hash, root);
		if (block)
		{
			if (auto permit = policy.reply_final (transaction, *block))
			{
				permits.push_back (*permit);
				stats.inc (nano::stat::type::vote_replier, nano::stat::detail::reply_final);
				logger.debug (nano::log::type::vote_replier, "Replying with final vote for: {} to: {}", block->hash (), channel);
			}
			else
			{
				stats.inc (nano::stat::type::vote_replier, nano::stat::detail::reply_skip);
				logger.debug (nano::log::type::vote_replier, "Skipping reply for: {} to: {} due to voting policy", hash, channel);
			}
		}
		else
		{
			stats.inc (nano::stat::type::vote_replier, nano::stat::detail::reply_unknown);
			logger.debug (nano::log::type::vote_replier, "Cannot reply for unknown block: {} to: {}", hash, channel);
		}
	}

	if (permits.empty ())
	{
		return;
	}

	stats.add (nano::stat::type::vote_replier, nano::stat::detail::reply_hashes, permits.size ());

	auto votes = policy.sign (nano::vote_type::final, permits, wallets.signer ());

	for (auto const & vote : votes)
	{
		nano::messages::confirm_ack msg{ network_constants, vote };
		channel->send (msg, nano::transport::traffic_type::vote_reply, [this] (auto & ec, auto size) {
			stats.inc (nano::stat::type::vote_replier_ec, to_stat_detail (ec), nano::stat::dir::out);
		});
	}
}

nano::container_info nano::vote_replier::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.add ("queue", queue.container_info ());
	return info;
}

/*
 * vote_replier_config
 */

nano::error nano::vote_replier_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("channel_limit", channel_limit, "Maximum number of queued requests per channel. \ntype:uint64");
	toml.put ("threads", threads, "Number of threads for request processing. \ntype:uint64");
	toml.put ("batch_size", batch_size, "Number of requests to process in a single batch. \ntype:uint64");

	return toml.get_error ();
}

nano::error nano::vote_replier_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("channel_limit", channel_limit);
	toml.get ("threads", threads);
	toml.get ("batch_size", batch_size);

	return toml.get_error ();
}
