#include <nano/lib/config.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/worker.hpp>
#include <nano/node/common.hpp>
#include <nano/node/node.hpp>
#include <nano/node/websocket.hpp>
#include <nano/node/websocket_payment_tracking.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/ledger.hpp>

void nano::websocket::payment_tracker::track (nano::websocket::payment_tracking_options const & options_a)
{
	auto tracked_l = tracked_accounts.lock ();
	std::chrono::seconds track_until (nano::seconds_since_epoch () + options_a.max_tracking_duration.count ());

	std::optional<nano::block_hash> block_hash_l;
	if (options_a.tracked_block)
	{
		block_hash_l = options_a.tracked_block->hash ();
	}

	// Add or update tracking information
	tracked_l->erase (options_a.tracked_account);
	tracked_l->try_emplace (options_a.tracked_account, options_a.id, block_hash_l, options_a.minimum_amount, options_a.tracking_policy, track_until);
}

void nano::websocket::payment_tracker::untrack (std::string const & account_a)
{
	auto tracked_l = tracked_accounts.lock ();
	tracked_l->erase (account_a);
}

void nano::websocket::payment_tracker::for_each (std::function<void(std::string const &, nano::websocket::payment_tracker::payment_tracking_info const &)> callback_a)
{
	// The callback may make changes to the tracked map (in order to untrack), so we take a snapshot.
	// This also enables us to quickly release the lock before the callbacks do their processing.
	std::unordered_map<std::string, payment_tracking_info> currently_tracked_l;
	{
		auto tracked_l = tracked_accounts.lock ();
		currently_tracked_l = *tracked_l;
	}

	for (auto const & [account_l, tracking_info_l] : currently_tracked_l)
	{
		callback_a (account_l, tracking_info_l);
	}
}

std::optional<nano::websocket::payment_tracker::payment_tracking_info> nano::websocket::payment_tracker::get_tracking_info (std::string const & account_a)
{
	auto tracked_l = tracked_accounts.lock ();
	auto const & match_l = tracked_l->find (account_a);
	if (match_l != tracked_l->end ())
	{
		return std::optional<nano::websocket::payment_tracker::payment_tracking_info> (match_l->second);
	}
	return std::nullopt;
}

bool nano::websocket::payment_tracker::update_partial_payment_amount (std::string const & account_a, nano::amount const & amount_a)
{
	auto different_amount_l (false);
	auto tracked_l = tracked_accounts.lock ();
	auto const & match_l = tracked_l->find (account_a);
	if (match_l != tracked_l->end ())
	{
		different_amount_l = match_l->second.last_sent_partial_amount != amount_a;
		if (different_amount_l)
		{
			match_l->second.last_sent_partial_amount = amount_a;
		}
	}

	return different_amount_l;
}

nano::websocket::payment_validator::payment_validator (boost::asio::io_context & io_ctx_a, nano::worker & worker_a, nano::ledger & ledger_a, nano::logger_mt & logger_a, std::function<void(std::shared_ptr<nano::block> const &, bool const)> publish_handler_a) :
worker (worker_a), ledger (ledger_a), logger (logger_a), publish_handler (publish_handler_a), payment_tracker_timer (io_ctx_a)
{
}

void nano::websocket::payment_validator::check_payment (nano::account const & destination_account_a, nano::block_hash const & block_hash_a, std::shared_ptr<nano::websocket::session> const & session_a)
{
	auto account_string_l (destination_account_a.to_account ());
	auto tracking_info_l (session_a->get_payment_tracker ().get_tracking_info (account_string_l));
	if (tracking_info_l)
	{
		nano::websocket::message_builder builder_l;
		nano::amount pending_l (0);
		nano::amount balance_l (0);

		auto tx_read_l (ledger.store.tx_begin_read ());
		nano::confirmation_height_info confirmation_height_info_l;
		std::optional<std::reference_wrapper<nano::confirmation_height_info>> optional_confirmation_height_info_l{ std::nullopt };

		// Get confirmed balance if available
		if (!ledger.store.confirmation_height_get (tx_read_l, destination_account_a, confirmation_height_info_l))
		{
			balance_l = ledger.balance (tx_read_l, confirmation_height_info_l.frontier);
		}

		// Sum up pending entries where the source send block is confirmed
		pending_l = ledger.account_pending_confirmed (tx_read_l, destination_account_a);

		if (tracking_info_l->tracking_policy == nano::websocket::payment_tracker::policy::account)
		{
			// Total confirmed balance
			nano::amount total_balance_l (pending_l.number () + balance_l.number ());

			if (total_balance_l.number () >= tracking_info_l->minimum_amount.number ())
			{
				auto notification = builder_l.payment_notification (*tracking_info_l, destination_account_a, balance_l, pending_l, optional_confirmation_height_info_l, false);
				session_a->write (notification);
				session_a->get_payment_tracker ().untrack (destination_account_a.to_account ());

				logger.always_log ("Websocket: sent payment notification for account: ", account_string_l);
			}
			else if (!total_balance_l.number ().is_zero ())
			{
				// Send partial payment notification if the amount is different than last time
				if (session_a->get_payment_tracker ().update_partial_payment_amount (destination_account_a.to_account (), total_balance_l))
				{
					auto notification = builder_l.payment_notification (*tracking_info_l, destination_account_a, balance_l, pending_l, optional_confirmation_height_info_l, true);
					session_a->write (notification);

					logger.always_log ("Websocket: sent partial payment notification for account: ", account_string_l);
				}
			}
		}
		else if (tracking_info_l->tracking_policy == nano::websocket::payment_tracker::policy::block)
		{
			if (ledger.block_confirmed (tx_read_l, block_hash_a) || (ledger.pruning && ledger.store.pruned_exists (tx_read_l, block_hash_a)))
			{
				auto notification_l = builder_l.payment_notification (*tracking_info_l, destination_account_a, balance_l, pending_l, optional_confirmation_height_info_l, false);
				session_a->write (notification_l);
				session_a->get_payment_tracker ().untrack (destination_account_a.to_account ());

				logger.always_log ("Websocket: sent payment notification for account: ", account_string_l, ", tracking block hash: ", block_hash_a.to_string ());
			}
		}
		else
		{
			debug_assert (false);
		}
	}
}

void nano::websocket::payment_validator::publish_block (std::shared_ptr<nano::block> block_a, bool const work_watcher_a)
{
	// Delegate to worker thread, as publishing the block involves a write transaction
	worker.push_task ([publish_handler = publish_handler, block_a, work_watcher_a]() {
		publish_handler (block_a, work_watcher_a);
	});
}

void nano::websocket::payment_validator::start (std::shared_ptr<nano::websocket::listener> const & websocket_server_a)
{
	websocket_server = websocket_server_a;
	ongoing_payment_tracking ();
}

void nano::websocket::payment_validator::ongoing_payment_tracking ()
{
	static nano::network_constants network_constants;
	payment_tracker_timer.expires_from_now (network_constants.is_dev_network () ? std::chrono::seconds (1) : std::chrono::seconds (5));
	payment_tracker_timer.async_wait ([this](const boost::system::error_code & ec) {
		if (!websocket_server->is_stopped () && !ec)
		{
			auto sessions_l (websocket_server->find_sessions (nano::websocket::topic::payment));
			for (auto & session_ptr : sessions_l)
			{
				std::vector<std::string> timed_out_l;
				session_ptr->get_payment_tracker ().for_each ([this, &timed_out_l, &session_ptr](std::string const & account_a, nano::websocket::payment_tracker::payment_tracking_info const & tracking_info_l) {
					nano::account destination_account_l;
					destination_account_l.decode_account (account_a);

					if (nano::seconds_since_epoch () > tracking_info_l.track_until.count ())
					{
						timed_out_l.push_back (account_a);
					}
					else
					{
						nano::block_hash block_hash_l;
						if (tracking_info_l.tracked_block_hash)
						{
							block_hash_l = *tracking_info_l.tracked_block_hash;
						}

						this->check_payment (destination_account_l, block_hash_l, session_ptr);
					}
				});

				// Remove timed-out trackings
				for (auto const & account : timed_out_l)
				{
					this->logger.always_log ("Websocket: payment tracking timed out for account: ", account);
					session_ptr->get_payment_tracker ().untrack (account);
				}
			}

			this->ongoing_payment_tracking ();
		}
	});
}
