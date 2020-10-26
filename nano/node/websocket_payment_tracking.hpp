#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>

#include <boost/asio/steady_timer.hpp>

#include <memory>
#include <string>

namespace nano
{
class node;
}

namespace nano::websocket
{
class session;
class payment_tracking_options;

/** Per-session tracking of payment destination accounts. This class is thread-safe. */
class payment_tracker
{
public:
	/** Payment tracking policies */
	enum policy
	{
		invalid,
		/** Track total balance of an account (account per payment use-case) */
		account,
		/** Track confirmation of a send state block (hand-off use-case) */
		block
	};

	/** Tracking info based on payment subscription options */
	class payment_tracking_info
	{
	public:
		payment_tracking_info (std::string id_a, std::shared_ptr<nano::state_block> const & tracked_block_a, nano::amount const & minimum_amount_a, nano::websocket::payment_tracker::policy tracking_policy_a, std::chrono::seconds track_until_a) :
		id (id_a), tracked_block (tracked_block_a), minimum_amount (minimum_amount_a), last_sent_partial_amount (0), tracking_policy (tracking_policy_a), track_until (track_until_a)
		{
		}

		/** The id provided through the Websocket subscription. This can be used by external systems to match up payment notifications. */
		std::string id;

		/** Tracked block, if any */
		std::shared_ptr<nano::state_block> tracked_block;

		/** The minimum amount required for a payment notification to be sent */
		nano::amount minimum_amount;

		/** If there's a partial payment (below minimum amount), we send a partial_payment notification. This is only done once per partial amount. */
		nano::amount last_sent_partial_amount;

		/** The requested tracking policy */
		nano::websocket::payment_tracker::policy tracking_policy;

		/** Tracking until this many seconds since epoch */
		std::chrono::seconds track_until;
	};

	/** Start tracking a destination account based on websocket options for the payment subscription */
	void track (nano::websocket::payment_tracking_options const & options_a);

	/** Stop tracking this destination account */
	void untrack (std::string const & account_a);

	/**
	 * Update the last-sent partial payment amount
	 * @param account_a Account being tracked
	 * @param amount_a The current total balance
	 * @return true if the amount is different from what is previously recorded. This causes a partial_payment message to be sent.
	 */
	bool update_partial_payment_amount (std::string const & account_a, nano::amount const & amount_a);

	/** Get tracking info for the given account, if available */
	std::optional<payment_tracking_info> get_tracking_info (std::string const & account_a);

	/** For each tracked account by this session, invoke \p callback_a */
	void for_each (std::function<void(std::string const &, payment_tracking_info const &)> callback_a);

private:
	nano::locked<std::unordered_map<std::string, payment_tracking_info>> tracked_accounts;
};

/** Interacts with the node to hand off send blocks, and queries the ledger for confirmation status and balances */
class payment_validator
{
public:
	/** Starts ongoing payment tracking */
	payment_validator (nano::node & node_a);

	/** Check if the payment conditions are met, and if so, send notification to websocket client */
	void check_payment (nano::account const & destination_account_a, nano::block_hash const & block_hash_a, std::shared_ptr<nano::websocket::session> const & session_a);

	/** Publish a send state block to the network */
	void publish_block (std::shared_ptr<nano::block> const & block_a, bool const work_watcher_a);

private:
	nano::node & node;
	boost::asio::steady_timer payment_tracker_timer;

	/** Periodically check tracked payments to handle cases where clients miss notifications or resubscribes. */
	void ongoing_payment_tracking ();
};
}
