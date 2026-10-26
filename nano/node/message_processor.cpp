#include <nano/lib/config.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/messages/messages.hpp>
#include <nano/node/message_dispatcher.hpp>
#include <nano/node/message_processor.hpp>

using namespace std::chrono_literals;

nano::message_processor::message_processor (message_processor_config const & config_a, nano::message_dispatcher & dispatcher_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ config_a },
	dispatcher{ dispatcher_a },
	stats{ stats_a },
	logger{ logger_a }
{
	queue.max_size_query = [this] (auto const & origin) {
		return config.max_queue;
	};

	queue.priority_query = [this] (auto const & origin) {
		return 1;
	};
}

nano::message_processor::~message_processor ()
{
	debug_assert (threads.empty ());
}

void nano::message_processor::start ()
{
	debug_assert (threads.empty ());

	for (int n = 0; n < config.threads; ++n)
	{
		threads.emplace_back ([this] () {
			nano::thread_role::set (nano::thread_role::name::message_processing);
			try
			{
				run ();
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
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
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

bool nano::message_processor::enqueue (std::unique_ptr<nano::messages::message> message, std::shared_ptr<nano::transport::channel> const & channel)
{
	release_assert (message != nullptr);
	release_assert (channel != nullptr);

	auto const type = message->type ();

	bool added = false;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		added = queue.push ({ std::move (message), channel }, { nano::no_value{}, channel });
	}
	if (added)
	{
		stats.inc (nano::stat::type::message_processor, nano::stat::detail::process);
		stats.inc (nano::stat::type::message_processor_type, to_stat_detail (type));

		condition.notify_all ();
	}
	else
	{
		stats.inc (nano::stat::type::message_processor, nano::stat::detail::overfill);
		stats.inc (nano::stat::type::message_processor_overfill, to_stat_detail (type));
	}
	return added;
}

void nano::message_processor::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::message_processor, nano::stat::detail::loop);

		if (!queue.empty ())
		{
			// Only log if component is under pressure
			if (queue.size () > nano::queue_warning_threshold () && log_interval.elapse (15s))
			{
				logger.info (nano::log::type::message_processor, "{} messages in processing queue", queue.size ());
			}

			run_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
		else
		{
			condition.wait (lock, [&] {
				return stopped || !queue.empty ();
			});
		}
	}
}

void nano::message_processor::run_batch (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	nano::timer<std::chrono::milliseconds> timer;
	timer.start ();

	size_t const max_batch_size = 1024 * 4;
	auto batch = queue.next_batch (max_batch_size);

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

	stats.inc (nano::stat::type::message, to_stat_detail (message.type ()), nano::stat::dir::in);
	logger.trace (nano::log::type::message, to_log_detail (message.type ()), nano::log::arg{ "message", message });

	dispatcher.dispatch (message, channel);
}

nano::container_info nano::message_processor::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.add ("queue", queue.container_info ());
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
