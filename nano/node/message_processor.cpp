#include <celerix/lib/thread_roles.hpp>
#include <celerix/node/bootstrap/bootstrap_service.hpp>
#include <celerix/node/message_processor.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/telemetry.hpp>
#include <celerix/secure/vote.hpp>

celerix::message_processor::message_processor (message_processor_config const & config_a, celerix::node & node_a) :
	config{ config_a },
	node{ node_a },
	stats{ node.stats },
	logger{ node.logger }
{
	queue.max_size_query = [this] (auto const & origin) {
		return config.max_queue;
	};

	queue.priority_query = [this] (auto const & origin) {
		return 1;
	};
}

celerix::message_processor::~message_processor ()
{
	debug_assert (threads.empty ());
}

void celerix::message_processor::start ()
{
	debug_assert (threads.empty ());

	for (int n = 0; n < config.threads; ++n)
	{
		threads.emplace_back ([this] () {
			celerix::thread_role::set (celerix::thread_role::name::message_processing);
			try
			{
				run ();
			}
			catch (boost::system::error_code & ec)
			{
				node.logger.critical (celerix::log::type::network, "Error: {}", ec.message ());
				release_assert (false);
			}
			catch (std::error_code & ec)
			{
				node.logger.critical (celerix::log::type::network, "Error: {}", ec.message ());
				release_assert (false);
			}
			catch (std::runtime_error & err)
			{
				node.logger.critical (celerix::log::type::network, "Error: {}", err.what ());
				release_assert (false);
			}
			catch (...)
			{
				node.logger.critical (celerix::log::type::network, "Unknown error");
				release_assert (false);
			}
		});
	}
}

void celerix::message_processor::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
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

bool celerix::message_processor::put (std::unique_ptr<celerix::message> message, std::shared_ptr<celerix::transport::channel> const & channel)
{
	release_assert (message != nullptr);
	release_assert (channel != nullptr);

	auto const type = message->type ();

	bool added = false;
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		added = queue.push ({ std::move (message), channel }, { celerix::no_value{}, channel });
	}
	if (added)
	{
		stats.inc (celerix::stat::type::message_processor, celerix::stat::detail::process);
		stats.inc (celerix::stat::type::message_processor_type, to_stat_detail (type));

		condition.notify_all ();
	}
	else
	{
		stats.inc (celerix::stat::type::message_processor, celerix::stat::detail::overfill);
		stats.inc (celerix::stat::type::message_processor_overfill, to_stat_detail (type));
	}
	return added;
}

void celerix::message_processor::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (celerix::stat::type::message_processor, celerix::stat::detail::loop);

		if (!queue.empty ())
		{
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

void celerix::message_processor::run_batch (celerix::unique_lock<celerix::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	celerix::timer<std::chrono::milliseconds> timer;
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
		logger.debug (celerix::log::type::message_processor, "Processed {} messages in {} milliseconds (rate of {} messages per second)",
		batch.size (),
		timer.since_start ().count (),
		((batch.size () * 1000ULL) / timer.value ().count ()));
	}
}

namespace
{
class process_visitor : public celerix::message_visitor
{
public:
	process_visitor (celerix::node & node_a, std::shared_ptr<celerix::transport::channel> const & channel_a) :
		node{ node_a },
		channel{ channel_a }
	{
	}

	void keepalive (celerix::keepalive const & message) override
	{
		// Check for self reported peering port
		auto self_report = message.peers[0];
		if (self_report.address () == boost::asio::ip::address_v6{} && self_report.port () != 0)
		{
			// Remember this for future forwarding to other peers
			celerix::endpoint peering_endpoint{ channel->get_remote_endpoint ().address (), self_report.port () };
			channel->set_peering_endpoint (peering_endpoint);
		}
	}

	void publish (celerix::publish const & message) override
	{
		// Put blocks that are being initially broadcasted in a separate queue, so that they won't have to compete with rebroadcasted blocks
		// Both queues have the same priority and size, so the potential for exploiting this is limited
		bool added = node.block_processor.add (message.block, message.is_originator () ? celerix::block_source::live_originator : celerix::block_source::live, channel);
		if (!added)
		{
			node.network.filter.clear (message.digest);
			node.stats.inc (celerix::stat::type::drop, celerix::stat::detail::publish, celerix::stat::dir::in);
		}
	}

	void confirm_req (celerix::confirm_req const & message) override
	{
		// Don't load nodes with disabled voting
		// TODO: This check should be cached somewhere
		if (node.config.enable_voting && node.wallets.reps ().voting > 0)
		{
			if (!message.roots_hashes.empty ())
			{
				node.aggregator.request (message.roots_hashes, channel);
			}
		}
	}

	void confirm_ack (celerix::confirm_ack const & message) override
	{
		// Ignore zero account votes
		if (message.vote->account.is_zero ())
		{
			node.stats.inc (celerix::stat::type::drop, celerix::stat::detail::confirm_ack_zero_account, celerix::stat::dir::in);
			return;
		}

		bool added = node.vote_processor.vote (message.vote, channel, message.is_rebroadcasted () ? celerix::vote_source::rebroadcast : celerix::vote_source::live);
		if (!added)
		{
			node.network.filter.clear (message.digest);
			node.stats.inc (celerix::stat::type::drop, celerix::stat::detail::confirm_ack, celerix::stat::dir::in);
		}
	}

	void bulk_pull (celerix::bulk_pull const &) override
	{
		debug_assert (false);
	}

	void bulk_pull_account (celerix::bulk_pull_account const &) override
	{
		debug_assert (false);
	}

	void bulk_push (celerix::bulk_push const &) override
	{
		debug_assert (false);
	}

	void frontier_req (celerix::frontier_req const &) override
	{
		debug_assert (false);
	}

	void node_id_handshake (celerix::node_id_handshake const & message) override
	{
		node.stats.inc (celerix::stat::type::message, celerix::stat::detail::node_id_handshake, celerix::stat::dir::in);
	}

	void telemetry_req (celerix::telemetry_req const & message) override
	{
		// Ignore telemetry requests as telemetry is being periodically broadcasted since V25+
	}

	void telemetry_ack (celerix::telemetry_ack const & message) override
	{
		node.telemetry.process (message, channel);
	}

	void asc_pull_req (celerix::asc_pull_req const & message) override
	{
		node.bootstrap_server.request (message, channel);
	}

	void asc_pull_ack (celerix::asc_pull_ack const & message) override
	{
		node.bootstrap.process (message, channel);
	}

private:
	celerix::node & node;
	std::shared_ptr<celerix::transport::channel> channel;
};
}

void celerix::message_processor::process (celerix::message const & message, std::shared_ptr<celerix::transport::channel> const & channel)
{
	release_assert (channel != nullptr);

	debug_assert (message.header.network == node.network_params.network.current_network);
	debug_assert (message.header.version_using >= node.network_params.network.protocol_version_min);

	stats.inc (celerix::stat::type::message, to_stat_detail (message.type ()), celerix::stat::dir::in);
	logger.trace (celerix::log::type::message, to_log_detail (message.type ()), celerix::log::arg{ "message", message });

	process_visitor visitor{ node, channel };
	message.visit (visitor);
}

celerix::container_info celerix::message_processor::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.add ("queue", queue.container_info ());
	return info;
}

/*
 * message_processor_config
 */

celerix::error celerix::message_processor_config::serialize (celerix::tomlconfig & toml) const
{
	toml.put ("threads", threads, "Number of threads to use for message processing. \ntype:uint64");
	toml.put ("max_queue", max_queue, "Maximum number of messages per peer to queue for processing. \ntype:uint64");

	return toml.get_error ();
}

celerix::error celerix::message_processor_config::deserialize (celerix::tomlconfig & toml)
{
	toml.get ("threads", threads);
	toml.get ("max_queue", max_queue);

	return toml.get_error ();
}
