#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/bootstrap/bootstrap.hpp>
#include <nano/node/bootstrap/bootstrap_attempt.hpp>
#include <nano/node/bootstrap/bootstrap_bulk_push.hpp>
#include <nano/node/bootstrap/bootstrap_frontier.hpp>
#include <nano/node/common.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/tcp.hpp>
#include <nano/node/websocket.hpp>

#include <boost/format.hpp>

#include <algorithm>

constexpr unsigned nano::bootstrap_limits::requeued_pulls_limit;
constexpr unsigned nano::bootstrap_limits::requeued_pulls_limit_dev;

nano::bootstrap_attempt::bootstrap_attempt (std::shared_ptr<nano::node> const & node_a, nano::bootstrap_mode mode_a, uint64_t incremental_id_a, std::string id_a) :
	node (node_a),
	incremental_id (incremental_id_a),
	id (id_a),
	mode (mode_a)
{
	if (id.empty ())
	{
		id = nano::hardened_constants::get ().random_128.to_string ();
	}

	node->logger.always_log (boost::str (boost::format ("Starting %1% bootstrap attempt with ID %2%") % mode_text () % id));
	node->bootstrap_initiator.notify_listeners (true);
	if (node->websocket_server)
	{
		nano::websocket::message_builder builder;
		node->websocket_server->broadcast (builder.bootstrap_started (id, mode_text ()));
	}
}

nano::bootstrap_attempt::~bootstrap_attempt ()
{
	node->logger.always_log (boost::str (boost::format ("Exiting %1% bootstrap attempt with ID %2%") % mode_text () % id));
	node->bootstrap_initiator.notify_listeners (false);
	if (node->websocket_server)
	{
		nano::websocket::message_builder builder;
		node->websocket_server->broadcast (builder.bootstrap_exited (id, mode_text (), attempt_start, total_blocks));
	}
}

bool nano::bootstrap_attempt::should_log ()
{
	nano::lock_guard<nano::mutex> guard (next_log_mutex);
	auto result (false);
	auto now (std::chrono::steady_clock::now ());
	if (next_log < now)
	{
		result = true;
		next_log = now + std::chrono::seconds (15);
	}
	return result;
}

bool nano::bootstrap_attempt::still_pulling ()
{
	debug_assert (!mutex.try_lock ());
	auto running (!stopped);
	auto still_pulling (pulling > 0);
	return running && still_pulling;
}

void nano::bootstrap_attempt::pull_started ()
{
	{
		nano::lock_guard<nano::mutex> guard (mutex);
		++pulling;
	}
	condition.notify_all ();
}

void nano::bootstrap_attempt::pull_finished ()
{
	{
		nano::lock_guard<nano::mutex> guard (mutex);
		--pulling;
	}
	condition.notify_all ();
}

void nano::bootstrap_attempt::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock (mutex);
		stopped = true;
	}
	condition.notify_all ();
	node->bootstrap_initiator.connections->clear_pulls (incremental_id);
}

std::string nano::bootstrap_attempt::mode_text ()
{
	std::string mode_text;
	if (mode == nano::bootstrap_mode::legacy)
	{
		mode_text = "legacy";
	}
	else if (mode == nano::bootstrap_mode::lazy)
	{
		mode_text = "lazy";
	}
	else if (mode == nano::bootstrap_mode::wallet_lazy)
	{
		mode_text = "wallet_lazy";
	}
	return mode_text;
}

void nano::bootstrap_attempt::add_frontier (nano::pull_info const &)
{
	debug_assert (mode == nano::bootstrap_mode::legacy);
}

void nano::bootstrap_attempt::add_bulk_push_target (nano::block_hash const &, nano::block_hash const &)
{
	debug_assert (mode == nano::bootstrap_mode::legacy);
}

bool nano::bootstrap_attempt::request_bulk_push_target (std::pair<nano::block_hash, nano::block_hash> &)
{
	debug_assert (mode == nano::bootstrap_mode::legacy);
	return true;
}

void nano::bootstrap_attempt::set_start_account (nano::account const &)
{
	debug_assert (mode == nano::bootstrap_mode::legacy);
}

bool nano::bootstrap_attempt::process_block (std::shared_ptr<nano::block> const & block_a, nano::account const & known_account_a, uint64_t pull_blocks_processed, nano::bulk_pull::count_t max_blocks, bool block_expected, unsigned retry_limit)
{
	bool stop_pull (false);
	// If block already exists in the ledger, then we can avoid next part of long account chain
	if (pull_blocks_processed % nano::bootstrap_limits::pull_count_per_check == 0 && node->ledger.block_or_pruned_exists (block_a->hash ()))
	{
		stop_pull = true;
	}
	else
	{
		nano::unchecked_info info (block_a, known_account_a, nano::signature_verification::unknown);
		node->block_processor.add (info);
	}
	return stop_pull;
}

uint32_t nano::bootstrap_attempt::lazy_batch_size ()
{
	debug_assert (mode == nano::bootstrap_mode::lazy);
	return node->network_params.bootstrap.lazy_min_pull_blocks;
}

bool nano::bootstrap_attempt::lazy_processed_or_exists (nano::block_hash const &)
{
	debug_assert (mode == nano::bootstrap_mode::lazy);
	return false;
}

bool nano::bootstrap_attempt::lazy_has_expired () const
{
	debug_assert (mode == nano::bootstrap_mode::lazy);
	return true;
}

void nano::bootstrap_attempt::requeue_pending (nano::account const &)
{
	debug_assert (mode == nano::bootstrap_mode::wallet_lazy);
}

void nano::bootstrap_attempt::wallet_start (std::deque<nano::account> &)
{
	debug_assert (mode == nano::bootstrap_mode::wallet_lazy);
}

std::size_t nano::bootstrap_attempt::wallet_size ()
{
	debug_assert (mode == nano::bootstrap_mode::wallet_lazy);
	return 0;
}
