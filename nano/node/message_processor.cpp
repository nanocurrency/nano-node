#include <nano/lib/config.hpp>
#include <nano/lib/hashing.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/messages/messages.hpp>
#include <nano/node/message_dispatcher.hpp>
#include <nano/node/message_processor.hpp>

#include <algorithm>

using namespace std::chrono_literals;

nano::message_processor::message_processor (message_processor_config const & config_a, nano::message_dispatcher & dispatcher_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ config_a },
	dispatcher{ dispatcher_a },
	stats{ stats_a },
	logger{ logger_a }
{
	// No threads would mean no processing at all, so there is always one shard
	auto const shard_count = std::max<size_t> (config.threads, 1);
	for (size_t n = 0; n < shard_count; ++n)
	{
		auto & shard = shards.emplace_back ();

		shard.queue.max_size_query = [this] (auto const & origin) {
			return config.max_queue;
		};

		shard.queue.priority_query = [this] (auto const & origin) {
			return 1;
		};
	}
}

nano::message_processor::~message_processor ()
{
	debug_assert (std::none_of (shards.begin (), shards.end (), [] (auto const & shard) { return shard.thread.joinable (); }));
}

void nano::message_processor::start ()
{
	for (auto & shard : shards)
	{
		debug_assert (!shard.thread.joinable ());

		shard.thread = std::thread ([this, &shard] () {
			nano::thread_role::set (nano::thread_role::name::message_processing);
			try
			{
				run (shard);
			}
			catch (boost::system::error_code & ec)
			{
				logger.critical (nano::log::type::network, "Error: {}", ec.message ());
				release_assert (false);
			}
			catch (std::error_code & ec)
			{
				logger.critical (nano::log::type::network, "Error: {}", ec.message ());
				release_assert (false);
			}
			catch (std::runtime_error & err)
			{
				logger.critical (nano::log::type::network, "Error: {}", err.what ());
				release_assert (false);
			}
			catch (...)
			{
				logger.critical (nano::log::type::network, "Unknown error");
				release_assert (false);
			}
		});
	}
}

void nano::message_processor::stop ()
{
	for (auto & shard : shards)
	{
		{
			nano::lock_guard<nano::mutex> lock{ shard.mutex };
			stopped = true;
		}
		shard.condition.notify_all ();
	}

	for (auto & shard : shards)
	{
		if (shard.thread.joinable ())
		{
			shard.thread.join ();
		}
	}
}

auto nano::message_processor::shard_for (std::shared_ptr<nano::transport::channel> const & channel) -> shard_state &
{
	return shards[nano::pointer_hash (channel.get ()) % shards.size ()];
}

bool nano::message_processor::enqueue (std::unique_ptr<nano::messages::message> message, std::shared_ptr<nano::transport::channel> const & channel)
{
	release_assert (message != nullptr);
	release_assert (channel != nullptr);

	auto const type = message->type ();
	auto & shard = shard_for (channel);

	bool added = false;
	{
		nano::lock_guard<nano::mutex> guard{ shard.mutex };
		added = shard.queue.push ({ std::move (message), channel }, { nano::no_value{}, channel });
	}
	if (added)
	{
		stats.inc (nano::stat::type::message_processor, nano::stat::detail::process);
		stats.inc (nano::stat::type::message_processor_type, to_stat_detail (type));

		shard.condition.notify_one ();
	}
	else
	{
		stats.inc (nano::stat::type::message_processor, nano::stat::detail::overfill);
		stats.inc (nano::stat::type::message_processor_overfill, to_stat_detail (type));
	}
	return added;
}

void nano::message_processor::run (shard_state & shard)
{
	nano::unique_lock<nano::mutex> lock{ shard.mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::message_processor, nano::stat::detail::loop);

		if (!shard.queue.empty ())
		{
			// Only log if component is under pressure
			if (shard.queue.size () > nano::queue_warning_threshold () && shard.log_interval.elapse (15s))
			{
				logger.info (nano::log::type::message_processor, "{} messages in processing queue", shard.queue.size ());
			}

			run_batch (shard, lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
		else
		{
			shard.condition.wait (lock, [&] {
				return stopped || !shard.queue.empty ();
			});
		}
	}
}

void nano::message_processor::run_batch (shard_state & shard, nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!shard.mutex.try_lock ());
	debug_assert (!shard.queue.empty ());

	nano::timer<std::chrono::milliseconds> timer;
	timer.start ();

	size_t const max_batch_size = 1024 * 4;
	auto batch = shard.queue.next_batch (max_batch_size);

	lock.unlock ();

	for (auto const & [entry, origin] : batch)
	{
		auto const & [message, channel] = entry;
		release_assert (message != nullptr);
		process (*message, channel);
	}

	if (timer.since_start () > std::chrono::milliseconds (100))
	{
		logger.debug (nano::log::type::message_processor, "Processed {} messages in {} milliseconds (rate of {} messages per second)",
		batch.size (),
		timer.since_start ().count (),
		((batch.size () * 1000ULL) / timer.value ().count ()));
	}
}

void nano::message_processor::process (nano::messages::message const & message, std::shared_ptr<nano::transport::channel> const & channel)
{
	release_assert (channel != nullptr);

	stats.inc (nano::stat::type::message_processor_dispatch, to_stat_detail (message.type ()));

	dispatcher.dispatch (message, channel);
}

nano::container_info nano::message_processor::container_info () const
{
	nano::container_info info;
	size_t index = 0;
	for (auto const & shard : shards)
	{
		nano::lock_guard<nano::mutex> guard{ shard.mutex };
		info.add ("shard_" + std::to_string (index++), shard.queue.container_info ());
	}
	return info;
}

/*
 * message_processor_config
 */

nano::error nano::message_processor_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("threads", threads, "Number of threads to use for message processing. \ntype:uint64");
	toml.put ("max_queue", max_queue, "Maximum number of messages per peer to queue for processing. \ntype:uint64");

	return toml.get_error ();
}

nano::error nano::message_processor_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("threads", threads);
	toml.get ("max_queue", max_queue);

	return toml.get_error ();
}
