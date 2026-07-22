#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/vote.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_relay.hpp>
#include <nano/node/vote_router.hpp>

#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

using namespace std::chrono_literals;

nano::vote_relay::vote_relay (vote_relay_config const & config_a, nano::vote_cache & vote_cache_a, nano::vote_router & vote_router_a, nano::rep_crawler & rep_crawler_a, nano::network_constants const & network_constants_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ config_a },
	vote_cache{ vote_cache_a },
	rep_crawler{ rep_crawler_a },
	network_constants{ network_constants_a },
	stats{ stats_a },
	logger{ logger_a }
{
	queue.max_size_query = [this] (auto const & origin) {
		return config.channel_limit;
	};
	queue.priority_query = [] (auto const & origin) {
		return 1;
	};

	vote_router_a.vote_processed.add ([this] (std::shared_ptr<nano::vote> const & vote, nano::vote_source source, auto const & results) {
		process_vote (vote);
	});
}

nano::vote_relay::~vote_relay ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::vote_relay::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enable)
	{
		return;
	}

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::vote_relay);
		run ();
	} };
}

void nano::vote_relay::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

bool nano::vote_relay::request (nano::messages::vote_relay_req const & message, std::shared_ptr<nano::transport::channel> const & channel)
{
	release_assert (channel != nullptr);
	debug_assert (!message.roots_hashes.empty ());

	if (!config.enable)
	{
		stats.inc (nano::stat::type::vote_relay, nano::stat::detail::drop);
		return false;
	}

	// TODO: Requests without a rep filter (votes from any representative) are reserved for the vote storage role
	if (message.reps.empty ())
	{
		stats.inc (nano::stat::type::vote_relay, nano::stat::detail::unsupported);
		return false;
	}

	bool added = false;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		added = queue.push (message, { nano::no_value{}, channel });
	}
	if (added)
	{
		stats.inc (nano::stat::type::vote_relay, nano::stat::detail::request);
		condition.notify_all ();
	}
	else
	{
		stats.inc (nano::stat::type::vote_relay, nano::stat::detail::overfill);
	}
	return added;
}

void nano::vote_relay::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::vote_relay, nano::stat::detail::loop);

		// Flush requests past their deadline with whatever votes they accumulated
		auto expired = index.evict (std::chrono::steady_clock::now ());
		if (!expired.empty ())
		{
			lock.unlock ();

			for (auto const & entry : expired)
			{
				stats.inc (nano::stat::type::vote_relay, nano::stat::detail::timeout);
				logger.debug (nano::log::type::vote_relay, "Request: {} timed out with {} votes collected", entry.id, entry.votes.size ());
				send_reply (entry.channel, entry.id, entry.votes);
			}

			lock.lock ();
		}

		// Process a batch of incoming requests
		if (!queue.empty ())
		{
			if (queue.size () > nano::queue_warning_threshold () && log_interval.elapse (15s))
			{
				logger.info (nano::log::type::vote_relay, "{} requests in processing queue", queue.size ());
			}

			run_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
		else // Idle until a request arrives or the earliest deadline needs flushing
		{
			if (auto deadline = index.next_deadline ())
			{
				auto const now = std::chrono::steady_clock::now ();
				if (*deadline > now)
				{
					condition.wait_for (lock, *deadline - now, [this] { return stopped || !queue.empty (); });
				}
			}
			else
			{
				condition.wait (lock, [this] { return stopped || !queue.empty (); });
			}
		}
	}
}

void nano::vote_relay::run_batch (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	debug_assert (config.batch_size > 0);
	auto batch = queue.next_batch (config.batch_size);

	lock.unlock ();

	for (auto const & [message, origin] : batch)
	{
		auto const & channel = origin.channel;

		if (!channel->max (nano::transport::traffic_type::vote_relay))
		{
			process (message, channel);
		}
		else
		{
			stats.inc (nano::stat::type::vote_relay, nano::stat::detail::channel_full);
		}
	}
}

/*
 * Serves a single relay request in two parts:
 * - votes already in the vote cache are acked to the requester immediately
 * - (hash, rep) pairs not answerable from the cache are registered in the index and the reps are queried with regular confirm_req
 * If nothing remains to wait for, the request is finished here with the terminating empty ack.
 * Otherwise the terminator is sent later: by process_vote () once all queried reps have answered, or by run () on timeout.
 */
void nano::vote_relay::process (nano::messages::vote_relay_req const & message, std::shared_ptr<nano::transport::channel> const & channel)
{
	debug_assert (!message.reps.empty ());

	stats.inc (nano::stat::type::vote_relay, nano::stat::detail::process);

	std::unordered_set<nano::account> const reps{ message.reps.begin (), message.reps.end () };
	bool const include_non_final = message.include_non_final ();

	// Scan the vote cache, splitting the request into votes to serve now (found) and per-hash reps that still need to answer (wants)
	std::vector<std::shared_ptr<nano::vote>> found;
	std::unordered_set<nano::vote const *> found_unique; // A single vote may satisfy multiple requested hashes but must be acked only once
	std::vector<nano::vote_relay_index::want> wants;
	std::unordered_set<nano::block_hash> processed_hashes;

	for (auto const & [hash, root] : message.roots_hashes)
	{
		if (!processed_hashes.insert (hash).second) // Ignore duplicate hashes within a request
		{
			continue;
		}

		// A cached vote counts only if it is from a requested rep and satisfies the finality requirement
		std::unordered_set<nano::account> satisfied;
		for (auto const & vote : vote_cache.find (hash))
		{
			if (!reps.contains (vote->account))
			{
				continue;
			}
			if (!vote->is_final () && !include_non_final)
			{
				continue;
			}
			satisfied.insert (vote->account);
			if (found_unique.insert (vote.get ()).second)
			{
				found.push_back (vote);
			}
		}

		// Requested reps not satisfied from the cache need to be queried for this hash
		std::vector<nano::account> missing;
		std::copy_if (reps.begin (), reps.end (), std::back_inserter (missing), [&satisfied] (auto const & rep) {
			return !satisfied.contains (rep);
		});
		if (!missing.empty ())
		{
			wants.push_back ({ hash, root, missing });
		}
	}

	stats.add (nano::stat::type::vote_relay, nano::stat::detail::cache, found.size ());

	// Reps without a channel in the rep crawler cannot be queried, drop them so the request does not idle waiting for votes that cannot arrive
	std::unordered_map<nano::account, std::shared_ptr<nano::transport::channel>> rep_channels;
	for (auto & want : wants)
	{
		std::erase_if (want.reps, [&] (auto const & rep) {
			auto it = rep_channels.find (rep);
			if (it == rep_channels.end ())
			{
				it = rep_channels.emplace (rep, rep_crawler.find (rep)).first;
				if (it->second == nullptr)
				{
					stats.inc (nano::stat::type::vote_relay, nano::stat::detail::rep_unknown);
				}
			}
			return it->second == nullptr;
		});
	}
	std::erase_if (wants, [] (auto const & want) {
		return want.reps.empty ();
	});

	// Register the pending request, insert returns the upstream queries to send with already in-flight (hash, rep) pairs filtered out
	// A vote arriving between the cache lookup above and this registration is missed, the request then relies on the timeout
	std::vector<nano::vote_relay_index::query> queries;
	bool tracked = false;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		if (!wants.empty () && !stopped && index.size () < config.max_requests)
		{
			queries = index.insert (channel, message.id, wants, include_non_final, std::chrono::steady_clock::now () + config.request_timeout);
			tracked = true;
		}
	}

	// Untracked means there is nothing to wait for (fully served from cache, only unreachable reps missing, or the index is full)
	if (!tracked)
	{
		if (!wants.empty ())
		{
			stats.inc (nano::stat::type::vote_relay, nano::stat::detail::queue_overflow);
		}
		send_reply (channel, message.id, found); // Cached votes + terminating empty ack, the request is finished
		return;
	}

	send_votes (channel, message.id, found); // Cached votes only, the terminator follows once the request completes or times out

	// Deduplicated (hash, rep) pairs ride on queries sent for earlier requests, the vote is shared with all waiters when it arrives
	auto const total = std::accumulate (wants.begin (), wants.end (), std::size_t{ 0 }, [] (auto acc, auto const & want) {
		return acc + want.reps.size ();
	});
	auto const queried = std::accumulate (queries.begin (), queries.end (), std::size_t{ 0 }, [] (auto acc, auto const & query) {
		return acc + query.roots_hashes.size ();
	});
	debug_assert (queried <= total);
	stats.add (nano::stat::type::vote_relay, nano::stat::detail::duplicate, total - queried);

	// Query the missing reps, their confirm_ack replies flow through vote_processor and reach this component via process_vote ()
	for (auto const & query : queries)
	{
		auto const & rep_channel = rep_channels.at (query.rep);
		release_assert (rep_channel != nullptr);

		stats.inc (nano::stat::type::vote_relay, nano::stat::detail::query);
		logger.debug (nano::log::type::vote_relay, "Querying rep: {} for {} hashes (request: {})", query.rep.to_account (), query.roots_hashes.size (), message.id);

		nano::messages::confirm_req req{ network_constants, query.roots_hashes };
		rep_channel->send (req, nano::transport::traffic_type::confirmation_requests);
	}
}

void nano::vote_relay::process_vote (std::shared_ptr<nano::vote> const & vote)
{
	if (!config.enable)
	{
		return;
	}

	std::vector<nano::vote_relay_index::reply> replies;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		if (stopped || index.empty ())
		{
			return;
		}
		replies = index.vote (vote);
	}
	for (auto const & reply : replies)
	{
		stats.inc (nano::stat::type::vote_relay, nano::stat::detail::done);
		logger.debug (nano::log::type::vote_relay, "Request: {} completed with {} votes", reply.id, reply.votes.size ());
		send_reply (reply.channel, reply.id, reply.votes);
	}
}

void nano::vote_relay::send_reply (std::shared_ptr<nano::transport::channel> const & channel, nano::vote_relay_index::id_t id, std::vector<std::shared_ptr<nano::vote>> const & votes)
{
	send_votes (channel, id, votes);

	// Terminate with an empty ack to signal that no more votes will be sent for this request
	stats.inc (nano::stat::type::vote_relay, nano::stat::detail::reply_empty);
	nano::messages::vote_relay_ack ack{ network_constants, id, {} };
	send_ack (channel, ack);
}

void nano::vote_relay::send_votes (std::shared_ptr<nano::transport::channel> const & channel, nano::vote_relay_index::id_t id, std::vector<std::shared_ptr<nano::vote>> const & votes)
{
	std::size_t constexpr base_size = sizeof (nano::vote_relay_index::id_t) + sizeof (uint8_t);

	std::vector<std::shared_ptr<nano::vote>> batch;
	std::size_t payload{ base_size };
	for (auto const & vote : votes)
	{
		auto const vote_size = nano::messages::vote_relay_ack::vote_size (vote);
		if (batch.size () >= nano::messages::vote_relay_ack::max_votes || payload + vote_size > nano::messages::vote_relay_ack::max_payload)
		{
			nano::messages::vote_relay_ack ack{ network_constants, id, batch };
			send_ack (channel, ack);
			batch.clear ();
			payload = base_size;
		}
		batch.push_back (vote);
		payload += vote_size;
	}
	if (!batch.empty ())
	{
		nano::messages::vote_relay_ack ack{ network_constants, id, batch };
		send_ack (channel, ack);
	}
}

void nano::vote_relay::send_ack (std::shared_ptr<nano::transport::channel> const & channel, nano::messages::vote_relay_ack const & ack)
{
	stats.inc (nano::stat::type::vote_relay, nano::stat::detail::reply);
	stats.add (nano::stat::type::vote_relay, nano::stat::detail::vote, ack.votes.size ());

	channel->send (ack, nano::transport::traffic_type::vote_relay);
	on_reply.notify (ack, channel);
}

std::size_t nano::vote_relay::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return queue.size ();
}

bool nano::vote_relay::empty () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return queue.empty ();
}

nano::container_info nano::vote_relay::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.add ("queue", queue.container_info ());
	info.put ("requests", index.size ());
	info.put ("pending", index.pending_size ());
	return info;
}

/*
 * vote_relay_config
 */

nano::error nano::vote_relay_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable the vote relay service and advertise it to peers. \ntype:bool");
	toml.put ("request_timeout", request_timeout.count (), "Time to wait for votes from representatives before finishing a request. \ntype:milliseconds");
	toml.put ("max_requests", max_requests, "Maximum number of requests waiting for representative votes. \ntype:uint64");
	toml.put ("channel_limit", channel_limit, "Maximum number of queued requests per channel. \ntype:uint64");
	toml.put ("batch_size", batch_size, "Number of requests to process in a single batch. \ntype:uint64");

	return toml.get_error ();
}

nano::error nano::vote_relay_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get_duration ("request_timeout", request_timeout);
	toml.get ("max_requests", max_requests);
	toml.get ("channel_limit", channel_limit);
	toml.get ("batch_size", batch_size);

	return toml.get_error ();
}
