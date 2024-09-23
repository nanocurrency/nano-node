#include <nano/lib/thread_roles.hpp>
#include <nano/node/bootstrap_ascending/service.hpp>
#include <nano/node/message_processor.hpp>
#include <nano/node/node.hpp>
#include <nano/node/telemetry.hpp>

nano::message_processor::message_processor (message_processor_config const & config_a, nano::node & node_a) :
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
				node.logger.critical (nano::log::type::network, "Error: {}", ec.message ());
				release_assert (false);
			}
			catch (std::error_code & ec)
			{
				node.logger.critical (nano::log::type::network, "Error: {}", ec.message ());
				release_assert (false);
			}
			catch (std::runtime_error & err)
			{
				node.logger.critical (nano::log::type::network, "Error: {}", err.what ());
				release_assert (false);
			}
			catch (...)
			{
				node.logger.critical (nano::log::type::network, "Unknown error");
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

bool nano::message_processor::put (std::unique_ptr<nano::message> message, std::shared_ptr<nano::transport::channel> const & channel)
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

namespace
{
class process_visitor : public nano::message_visitor
{
public:
	process_visitor (nano::node & node_a, std::shared_ptr<nano::transport::channel> const & channel_a) :
		node{ node_a },
		channel{ channel_a }
	{
	}

	void keepalive (nano::keepalive const & message) override
	{
		// Check for special node port data
		auto peer0 (message.peers[0]);
		if (peer0.address () == boost::asio::ip::address_v6{} && peer0.port () != 0)
		{
			// TODO: Remove this as we do not need to establish a second connection to the same peer
			nano::endpoint new_endpoint (channel->get_tcp_endpoint ().address (), peer0.port ());
			node.network.merge_peer (new_endpoint);

			// Remember this for future forwarding to other peers
			channel->set_peering_endpoint (new_endpoint);
		}
	}

	void publish (nano::publish const & message) override
	{
		// Put blocks that are being initially broadcasted in a separate queue, so that they won't have to compete with rebroadcasted blocks
		// Both queues have the same priority and size, so the potential for exploiting this is limited
		bool added = node.block_processor.add (message.block, message.is_originator () ? nano::block_source::live_originator : nano::block_source::live, channel);
		if (!added)
		{
			node.network.publish_filter.clear (message.digest);
			node.stats.inc (nano::stat::type::drop, nano::stat::detail::publish, nano::stat::dir::in);
		}
	}

	void confirm_req (nano::confirm_req const & message) override
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

	void confirm_ack (nano::confirm_ack const & message) override
	{
		// Ignore zero account votes
		if (message.vote->account.is_zero ())
		{
			node.stats.inc (nano::stat::type::drop, nano::stat::detail::confirm_ack_zero_account, nano::stat::dir::in);
			return;
		}

		bool added = node.vote_processor.vote (message.vote, channel, message.is_rebroadcasted () ? nano::vote_source::rebroadcast : nano::vote_source::live);
		if (!added)
		{
			node.network.publish_filter.clear (message.digest);
			node.stats.inc (nano::stat::type::drop, nano::stat::detail::confirm_ack, nano::stat::dir::in);
		}
	}

	void bulk_pull (nano::bulk_pull const &) override
	{
		debug_assert (false);
	}

	void bulk_pull_account (nano::bulk_pull_account const &) override
	{
		debug_assert (false);
	}

	void bulk_push (nano::bulk_push const &) override
	{
		debug_assert (false);
	}

	void frontier_req (nano::frontier_req const &) override
	{
		debug_assert (false);
	}

	void node_id_handshake (nano::node_id_handshake const & message) override
	{
		node.stats.inc (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in);
	}

	void telemetry_req (nano::telemetry_req const & message) override
	{
		// Ignore telemetry requests as telemetry is being periodically broadcasted since V25+
	}

	void telemetry_ack (nano::telemetry_ack const & message) override
	{
		node.telemetry.process (message, channel);
	}

	void asc_pull_req (nano::asc_pull_req const & message) override
	{
		node.bootstrap_server.request (message, channel);
	}

	void asc_pull_ack (nano::asc_pull_ack const & message) override
	{
		node.ascendboot.process (message, channel);
	}

private:
	nano::node & node;
	std::shared_ptr<nano::transport::channel> channel;
};
}

void nano::message_processor::process (nano::message const & message, std::shared_ptr<nano::transport::channel> const & channel)
{
	release_assert (channel != nullptr);

	debug_assert (message.header.network == node.network_params.network.current_network);
	debug_assert (message.header.version_using >= node.network_params.network.protocol_version_min);

	stats.inc (nano::stat::type::message, to_stat_detail (message.type ()), nano::stat::dir::in);
	logger.trace (nano::log::type::message, to_log_detail (message.type ()), nano::log::arg{ "message", message });

	process_visitor visitor{ node, channel };
	message.visit (visitor);
}

std::unique_ptr<nano::container_info_component> nano::message_processor::collect_container_info (std::string const & name)
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (queue.collect_container_info ("queue"));
	return composite;
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