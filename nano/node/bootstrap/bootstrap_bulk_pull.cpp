#include <nano/lib/blocks.hpp>
#include <nano/node/bootstrap/block_deserializer.hpp>
#include <nano/node/bootstrap/bootstrap.hpp>
#include <nano/node/bootstrap/bootstrap_bulk_pull.hpp>
#include <nano/node/bootstrap/bootstrap_connections.hpp>
#include <nano/node/bootstrap/bootstrap_lazy.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/tcp.hpp>

#include <boost/format.hpp>

nano::pull_info::pull_info (nano::hash_or_account const & account_or_head_a, nano::block_hash const & head_a, nano::block_hash const & end_a, uint64_t bootstrap_id_a, count_t count_a, unsigned retry_limit_a) :
	account_or_head (account_or_head_a),
	head (head_a),
	head_original (head_a),
	end (end_a),
	count (count_a),
	retry_limit (retry_limit_a),
	bootstrap_id (bootstrap_id_a)
{
}

nano::bulk_pull_client::bulk_pull_client (std::shared_ptr<nano::bootstrap_client> const & connection_a, std::shared_ptr<nano::bootstrap_attempt> const & attempt_a, nano::pull_info const & pull_a) :
	connection{ connection_a },
	attempt{ attempt_a },
	pull{ pull_a },
	block_deserializer{ std::make_shared<nano::bootstrap::block_deserializer> () }
{
	attempt->condition.notify_all ();
}

nano::bulk_pull_client::~bulk_pull_client ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	/* If received end block is not expected end block
	Or if given start and end blocks are from different chains (i.e. forked node or malicious node) */
	if (expected != pull.end && !expected.is_zero ())
	{
		pull.head = expected;
		if (attempt->mode != nano::bootstrap_mode::legacy)
		{
			pull.account_or_head = expected;
		}
		pull.processed += pull_blocks - unexpected_count;
		node->bootstrap_initiator.connections->requeue_pull (pull, network_error);

		node->logger.debug (nano::log::type::bulk_pull_client, "Bulk pull end block is not expected {} for account {} or head block {}", pull.end.to_string (), pull.account_or_head.to_account (), pull.account_or_head.to_string ());
	}
	else
	{
		node->bootstrap_initiator.cache.remove (pull);
	}
	attempt->pull_finished ();
}

void nano::bulk_pull_client::request ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	debug_assert (!pull.head.is_zero () || pull.retry_limit <= node->network_params.bootstrap.lazy_retry_limit);
	expected = pull.head;
	nano::bulk_pull req{ node->network_params.network };
	if (pull.head == pull.head_original && pull.attempts % 4 < 3)
	{
		// Account for new pulls
		req.start = pull.account_or_head;
	}
	else
	{
		// Head for cached pulls or accounts with public key equal to existing block hash (25% of attempts)
		req.start = pull.head;
	}
	req.end = pull.end;
	req.count = pull.count;
	req.set_count_present (pull.count != 0);

	node->logger.trace (nano::log::type::bulk_pull_client, nano::log::detail::requesting_account_or_head,
	nano::log::arg{ "account_or_head", pull.account_or_head },
	nano::log::arg{ "channel", connection->channel });

	if (attempt->should_log ())
	{
		node->logger.debug (nano::log::type::bulk_pull_client, "Accounts in pull queue: {}", attempt->pulling.load ());
	}

	auto this_l (shared_from_this ());
	connection->channel->send (
	req, [this_l] (boost::system::error_code const & ec, std::size_t size_a) {
		auto node = this_l->connection->node.lock ();
		if (!node)
		{
			return;
		}
		if (!ec)
		{
			this_l->throttled_receive_block ();
		}
		else
		{
			node->logger.debug (nano::log::type::bulk_pull_client, "Error sending bulk pull request to: {} ({})", this_l->connection->channel->to_string (), ec.message ());
			node->stats.inc (nano::stat::type::bootstrap, nano::stat::detail::bulk_pull_request_failure, nano::stat::dir::in);
		}
	},
	nano::transport::buffer_drop_policy::no_limiter_drop);
}

void nano::bulk_pull_client::throttled_receive_block ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	debug_assert (!network_error);
	if (!node->block_processor.half_full () && !node->block_processor.flushing)
	{
		receive_block ();
	}
	else
	{
		auto this_l (shared_from_this ());
		node->workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::seconds (1), [this_l] () {
			if (!this_l->connection->pending_stop && !this_l->attempt->stopped)
			{
				this_l->throttled_receive_block ();
			}
		});
	}
}

void nano::bulk_pull_client::receive_block ()
{
	block_deserializer->read (*connection->socket, [this_l = shared_from_this ()] (boost::system::error_code ec, std::shared_ptr<nano::block> block) {
		this_l->received_block (ec, block);
	});
}

void nano::bulk_pull_client::received_block (boost::system::error_code ec, std::shared_ptr<nano::block> block)
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	if (ec)
	{
		network_error = true;
		return;
	}
	if (block == nullptr)
	{
		// Avoid re-using slow peers, or peers that sent the wrong blocks.
		if (!connection->pending_stop && (expected == pull.end || (pull.count != 0 && pull.count == pull_blocks)))
		{
			node->bootstrap_initiator.connections->pool_connection (connection);
		}
		return;
	}
	if (node->network_params.work.validate_entry (*block))
	{
		node->logger.debug (nano::log::type::bulk_pull_client, "Insufficient work for bulk pull block: {}", block->hash ().to_string ());
		node->stats.inc (nano::stat::type::error, nano::stat::detail::insufficient_work);
		return;
	}
	auto hash = block->hash ();

	node->logger.trace (nano::log::type::bulk_pull_client, nano::log::detail::pulled_block, nano::log::arg{ "block", block });

	// Is block expected?
	bool block_expected (false);
	// Unconfirmed head is used only for lazy destinations if legacy bootstrap is not available, see nano::bootstrap_attempt::lazy_destinations_increment (...)
	bool unconfirmed_account_head = node->flags.disable_legacy_bootstrap && pull_blocks == 0 && pull.retry_limit <= node->network_params.bootstrap.lazy_retry_limit && (expected == pull.account_or_head.as_block_hash ()) && (block->account () == pull.account_or_head.as_account ());
	if (hash == expected || unconfirmed_account_head)
	{
		expected = block->previous ();
		block_expected = true;
	}
	else
	{
		unexpected_count++;
	}
	if (pull_blocks == 0 && block_expected)
	{
		known_account = block->account ().value_or (0);
	}
	if (connection->block_count++ == 0)
	{
		connection->set_start_time (std::chrono::steady_clock::now ());
	}
	attempt->total_blocks++;
	pull_blocks++;
	bool stop_pull (attempt->process_block (block, known_account, pull_blocks, pull.count, block_expected, pull.retry_limit));
	if (!stop_pull && !connection->hard_stop.load ())
	{
		/* Process block in lazy pull if not stopped
		Stop usual pull request with unexpected block & more than 16k blocks processed
		to prevent spam */
		if (attempt->mode != nano::bootstrap_mode::legacy || unexpected_count < 16384)
		{
			throttled_receive_block ();
		}
	}
	else if (!stop_pull && block_expected)
	{
		node->bootstrap_initiator.connections->pool_connection (connection);
	}
}

nano::bulk_pull_account_client::bulk_pull_account_client (std::shared_ptr<nano::bootstrap_client> const & connection_a, std::shared_ptr<nano::bootstrap_attempt_wallet> const & attempt_a, nano::account const & account_a) :
	connection (connection_a),
	attempt (attempt_a),
	account (account_a),
	pull_blocks (0)
{
	attempt->condition.notify_all ();
}

nano::bulk_pull_account_client::~bulk_pull_account_client ()
{
	attempt->pull_finished ();
}

void nano::bulk_pull_account_client::request ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	nano::bulk_pull_account req{ node->network_params.network };
	req.account = account;
	req.minimum_amount = node->config.receive_minimum;
	req.flags = nano::bulk_pull_account_flags::pending_hash_and_amount;

	node->logger.trace (nano::log::type::bulk_pull_account_client, nano::log::detail::requesting_pending,
	nano::log::arg{ "account", req.account.to_account () }, // TODO: Convert to lazy eval
	nano::log::arg{ "connection", connection->channel });

	if (attempt->should_log ())
	{
		node->logger.debug (nano::log::type::bulk_pull_account_client, "Accounts in pull queue: {}", attempt->wallet_size ());
	}

	auto this_l (shared_from_this ());
	connection->channel->send (
	req, [this_l] (boost::system::error_code const & ec, std::size_t size_a) {
		auto node = this_l->connection->node.lock ();
		if (!node)
		{
			return;
		}
		if (!ec)
		{
			this_l->receive_pending ();
		}
		else
		{
			node->logger.debug (nano::log::type::bulk_pull_account_client, "Error starting bulk pull request to: {} ({})", this_l->connection->channel->to_string (), ec.message ());
			node->stats.inc (nano::stat::type::bootstrap, nano::stat::detail::bulk_pull_error_starting_request, nano::stat::dir::in);

			this_l->attempt->requeue_pending (this_l->account);
		}
	},
	nano::transport::buffer_drop_policy::no_limiter_drop);
}

void nano::bulk_pull_account_client::receive_pending ()
{
	auto this_l (shared_from_this ());
	std::size_t size_l (sizeof (nano::uint256_union) + sizeof (nano::uint128_union));
	connection->socket->async_read (connection->receive_buffer, size_l, [this_l, size_l] (boost::system::error_code const & ec, std::size_t size_a) {
		auto node = this_l->connection->node.lock ();
		if (!node)
		{
			return;
		}
		// An issue with asio is that sometimes, instead of reporting a bad file descriptor during disconnect,
		// we simply get a size of 0.
		if (size_a == size_l)
		{
			if (!ec)
			{
				nano::block_hash pending;
				nano::bufferstream frontier_stream (this_l->connection->receive_buffer->data (), sizeof (nano::uint256_union));
				auto error1 (nano::try_read (frontier_stream, pending));
				(void)error1;
				debug_assert (!error1);
				nano::amount balance;
				nano::bufferstream balance_stream (this_l->connection->receive_buffer->data () + sizeof (nano::uint256_union), sizeof (nano::uint128_union));
				auto error2 (nano::try_read (balance_stream, balance));
				(void)error2;
				debug_assert (!error2);
				if (this_l->pull_blocks == 0 || !pending.is_zero ())
				{
					if (this_l->pull_blocks == 0 || balance.number () >= node->config.receive_minimum.number ())
					{
						this_l->pull_blocks++;
						{
							if (!pending.is_zero ())
							{
								if (!node->ledger.block_or_pruned_exists (pending))
								{
									node->bootstrap_initiator.bootstrap_lazy (pending, false);
								}
							}
						}
						this_l->receive_pending ();
					}
					else
					{
						this_l->attempt->requeue_pending (this_l->account);
					}
				}
				else
				{
					node->bootstrap_initiator.connections->pool_connection (this_l->connection);
				}
			}
			else
			{
				node->logger.debug (nano::log::type::bulk_pull_account_client, "Error while receiving bulk pull account frontier: {}", ec.message ());

				this_l->attempt->requeue_pending (this_l->account);
			}
		}
		else
		{
			node->logger.debug (nano::log::type::bulk_pull_account_client, "Invalid size: Expected {}, got: {}", size_l, size_a);

			this_l->attempt->requeue_pending (this_l->account);
		}
	});
}

/**
 * Handle a request for the pull of all blocks associated with an account
 * The account is supplied as the "start" member, and the final block to
 * send is the "end" member.  The "start" member may also be a block
 * hash, in which case the that hash is used as the start of a chain
 * to send.  To determine if "start" is interpretted as an account or
 * hash, the ledger is checked to see if the block specified exists,
 * if not then it is interpretted as an account.
 *
 * Additionally, if "start" is specified as a block hash the range
 * is inclusive of that block hash, that is the range will be:
 * [start, end); In the case that a block hash is not specified the
 * range will be exclusive of the frontier for that account with
 * a range of (frontier, end)
 */
void nano::bulk_pull_server::set_current_end ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	include_start = false;
	debug_assert (request != nullptr);
	auto transaction (node->store.tx_begin_read ());
	if (!node->ledger.block_exists (transaction, request->end))
	{
		node->logger.debug (nano::log::type::bulk_pull_server, "Bulk pull end block doesn't exist: {}, sending everything", request->end.to_string ());

		request->end.clear ();
	}

	if (node->ledger.block_exists (transaction, request->start.as_block_hash ()))
	{
		node->logger.debug (nano::log::type::bulk_pull_server, "Bulk pull request for block hash: {}", request->start.to_string ());

		current = ascending () ? node->store.block.successor (transaction, request->start.as_block_hash ()) : request->start.as_block_hash ();
		include_start = true;
	}
	else
	{
		auto info = node->ledger.account_info (transaction, request->start.as_account ());
		if (!info)
		{
			node->logger.debug (nano::log::type::bulk_pull_server, "Request for unknown account: {}", request->start.to_account ());

			current = request->end;
		}
		else
		{
			current = ascending () ? info->open_block : info->head;
			if (!request->end.is_zero ())
			{
				auto account (node->ledger.account (transaction, request->end));
				if (account != request->start.as_account ())
				{
					node->logger.debug (nano::log::type::bulk_pull_server, "Request for block that is not on account chain: {} not on {}", request->end.to_string (), request->start.to_account ());

					current = request->end;
				}
			}
		}
	}

	sent_count = 0;
	if (request->is_count_present ())
	{
		max_count = request->count;
	}
	else
	{
		max_count = 0;
	}
}

void nano::bulk_pull_server::send_next ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	auto block = get_next ();
	if (block != nullptr)
	{
		node->logger.trace (nano::log::type::bulk_pull_server, nano::log::detail::sending_block,
		nano::log::arg{ "block", block },
		nano::log::arg{ "socket", connection->socket });

		std::vector<uint8_t> send_buffer;
		{
			nano::vectorstream stream (send_buffer);
			nano::serialize_block (stream, *block);
		}

		connection->socket->async_write (nano::shared_const_buffer (std::move (send_buffer)), [this_l = shared_from_this ()] (boost::system::error_code const & ec, std::size_t size_a) {
			this_l->sent_action (ec, size_a);
		});
	}
	else
	{
		send_finished ();
	}
}

std::shared_ptr<nano::block> nano::bulk_pull_server::get_next ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return nullptr;
	}
	std::shared_ptr<nano::block> result;
	bool send_current = false, set_current_to_end = false;

	/*
	 * Determine if we should reply with a block
	 *
	 * If our cursor is on the final block, we should signal that we
	 * are done by returning a null result.
	 *
	 * Unless we are including the "start" member and this is the
	 * start member, then include it anyway.
	 */
	if (current != request->end)
	{
		send_current = true;
	}
	else if (current == request->end && include_start == true)
	{
		send_current = true;

		/*
		 * We also need to ensure that the next time
		 * are invoked that we return a null result
		 */
		set_current_to_end = true;
	}

	/*
	 * Account for how many blocks we have provided.  If this
	 * exceeds the requested maximum, return an empty object
	 * to signal the end of results
	 */
	if (max_count != 0 && sent_count >= max_count)
	{
		send_current = false;
	}

	if (send_current)
	{
		result = node->block (current);
		if (result != nullptr && set_current_to_end == false)
		{
			auto next = ascending () ? result->sideband ().successor : result->previous ();
			if (!next.is_zero ())
			{
				current = next;
			}
			else
			{
				current = request->end;
			}
		}
		else
		{
			current = request->end;
		}

		sent_count++;
	}

	/*
	 * Once we have processed "get_next()" once our cursor is no longer on
	 * the "start" member, so this flag is not relevant is always false.
	 */
	include_start = false;

	return result;
}

void nano::bulk_pull_server::sent_action (boost::system::error_code const & ec, std::size_t size_a)
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	if (!ec)
	{
		node->bootstrap_workers.push_task ([this_l = shared_from_this ()] () {
			this_l->send_next ();
		});
	}
	else
	{
		node->logger.debug (nano::log::type::bulk_pull_server, "Unable to bulk send block: {}", ec.message ());
	}
}

void nano::bulk_pull_server::send_finished ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	nano::shared_const_buffer send_buffer (static_cast<uint8_t> (nano::block_type::not_a_block));
	auto this_l (shared_from_this ());

	node->logger.debug (nano::log::type::bulk_pull_server, "Bulk sending finished");

	connection->socket->async_write (send_buffer, [this_l] (boost::system::error_code const & ec, std::size_t size_a) {
		this_l->no_block_sent (ec, size_a);
	});
}

void nano::bulk_pull_server::no_block_sent (boost::system::error_code const & ec, std::size_t size_a)
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	if (!ec)
	{
		debug_assert (size_a == 1);
		connection->start ();
	}
	else
	{
		node->logger.debug (nano::log::type::bulk_pull_server, "Unable to bulk send not-a-block: {}", ec.message ());
	}
}

bool nano::bulk_pull_server::ascending () const
{
	return request->header.bulk_pull_ascending ();
}

nano::bulk_pull_server::bulk_pull_server (std::shared_ptr<nano::transport::tcp_server> const & connection_a, std::unique_ptr<nano::bulk_pull> request_a) :
	connection (connection_a),
	request (std::move (request_a))
{
	set_current_end ();
}

/**
 * Bulk pull blocks related to an account
 */
void nano::bulk_pull_account_server::set_params ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	debug_assert (request != nullptr);

	/*
	 * Parse the flags
	 */
	invalid_request = false;
	pending_include_address = false;
	pending_address_only = false;
	if (request->flags == nano::bulk_pull_account_flags::pending_address_only)
	{
		pending_address_only = true;
	}
	else if (request->flags == nano::bulk_pull_account_flags::pending_hash_amount_and_address)
	{
		/**
		 ** This is the same as "pending_hash_and_amount" but with the
		 ** sending address appended, for UI purposes mainly.
		 **/
		pending_include_address = true;
	}
	else if (request->flags == nano::bulk_pull_account_flags::pending_hash_and_amount)
	{
		/** The defaults are set above **/
	}
	else
	{
		node->logger.debug (nano::log::type::bulk_pull_account_server, "Invalid bulk_pull_account flags supplied: {}", static_cast<uint8_t> (request->flags));

		invalid_request = true;
		return;
	}

	/*
	 * Initialize the current item from the requested account
	 */
	current_key.account = request->account;
	current_key.hash = 0;
}

void nano::bulk_pull_account_server::send_frontier ()
{
	/*
	 * This function is really the entry point into this class,
	 * so handle the invalid_request case by terminating the
	 * request without any response
	 */
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	if (!invalid_request)
	{
		auto stream_transaction (node->store.tx_begin_read ());

		// Get account balance and frontier block hash
		auto account_frontier_hash (node->ledger.latest (stream_transaction, request->account));
		auto account_frontier_balance_int (node->ledger.account_balance (stream_transaction, request->account));
		nano::uint128_union account_frontier_balance (account_frontier_balance_int);

		// Write the frontier block hash and balance into a buffer
		std::vector<uint8_t> send_buffer;
		{
			nano::vectorstream output_stream (send_buffer);
			write (output_stream, account_frontier_hash.bytes);
			write (output_stream, account_frontier_balance.bytes);
		}

		// Send the buffer to the requestor
		auto this_l (shared_from_this ());
		connection->socket->async_write (nano::shared_const_buffer (std::move (send_buffer)), [this_l] (boost::system::error_code const & ec, std::size_t size_a) {
			this_l->sent_action (ec, size_a);
		});
	}
}

void nano::bulk_pull_account_server::send_next_block ()
{
	/*
	 * Get the next item from the queue, it is a tuple with the key (which
	 * contains the account and hash) and data (which contains the amount)
	 */
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	auto block_data (get_next ());
	auto block_info_key (block_data.first.get ());
	auto block_info (block_data.second.get ());

	if (block_info_key != nullptr)
	{
		/*
		 * If we have a new item, emit it to the socket
		 */

		std::vector<uint8_t> send_buffer;
		if (pending_address_only)
		{
			node->logger.trace (nano::log::type::bulk_pull_account_server, nano::log::detail::sending_pending,
			nano::log::arg{ "pending", block_info->source });

			nano::vectorstream output_stream (send_buffer);
			write (output_stream, block_info->source.bytes);
		}
		else
		{
			node->logger.trace (nano::log::type::bulk_pull_account_server, nano::log::detail::sending_block,
			nano::log::arg{ "block", block_info_key->hash });

			nano::vectorstream output_stream (send_buffer);
			write (output_stream, block_info_key->hash.bytes);
			write (output_stream, block_info->amount.bytes);

			if (pending_include_address)
			{
				/**
				 ** Write the source address as well, if requested
				 **/
				write (output_stream, block_info->source.bytes);
			}
		}

		auto this_l (shared_from_this ());
		connection->socket->async_write (nano::shared_const_buffer (std::move (send_buffer)), [this_l] (boost::system::error_code const & ec, std::size_t size_a) {
			this_l->sent_action (ec, size_a);
		});
	}
	else
	{
		/*
		 * Otherwise, finalize the connection
		 */
		node->logger.debug (nano::log::type::bulk_pull_account_server, "Done sending blocks");

		send_finished ();
	}
}

std::pair<std::unique_ptr<nano::pending_key>, std::unique_ptr<nano::pending_info>> nano::bulk_pull_account_server::get_next ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return { nullptr, nullptr };
	}
	std::pair<std::unique_ptr<nano::pending_key>, std::unique_ptr<nano::pending_info>> result;

	while (true)
	{
		/*
		 * For each iteration of this loop, establish and then
		 * destroy a database transaction, to avoid locking the
		 * database for a prolonged period.
		 */
		auto stream_transaction (node->store.tx_begin_read ());
		auto stream (node->store.pending.begin (stream_transaction, current_key));

		if (stream == store::iterator<nano::pending_key, nano::pending_info> (nullptr))
		{
			break;
		}

		nano::pending_key key (stream->first);
		nano::pending_info info (stream->second);

		/*
		 * Get the key for the next value, to use in the next call or iteration
		 */
		current_key.account = key.account;
		current_key.hash = key.hash.number () + 1;

		/*
		 * Finish up if the response is for a different account
		 */
		if (key.account != request->account)
		{
			break;
		}

		/*
		 * Skip entries where the amount is less than the requested
		 * minimum
		 */
		if (info.amount < request->minimum_amount)
		{
			continue;
		}

		/*
		 * If the pending_address_only flag is set, de-duplicate the
		 * responses.  The responses are the address of the sender,
		 * so they are are part of the pending table's information
		 * and not key, so we have to de-duplicate them manually.
		 */
		if (pending_address_only)
		{
			if (!deduplication.insert (info.source).second)
			{
				/*
				 * If the deduplication map gets too
				 * large, clear it out.  This may
				 * result in some duplicates getting
				 * sent to the client, but we do not
				 * want to commit too much memory
				 */
				if (deduplication.size () > 4096)
				{
					deduplication.clear ();
				}
				continue;
			}
		}

		result.first = std::make_unique<nano::pending_key> (key);
		result.second = std::make_unique<nano::pending_info> (info);

		break;
	}

	return result;
}

void nano::bulk_pull_account_server::sent_action (boost::system::error_code const & ec, std::size_t size_a)
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	if (!ec)
	{
		node->bootstrap_workers.push_task ([this_l = shared_from_this ()] () {
			this_l->send_next_block ();
		});
	}
	else
	{
		node->logger.debug (nano::log::type::bulk_pull_account_server, "Unable to bulk send block: {}", ec.message ());
	}
}

void nano::bulk_pull_account_server::send_finished ()
{
	/*
	 * The "bulk_pull_account" final sequence is a final block of all
	 * zeros.  If we are sending only account public keys (with the
	 * "pending_address_only" flag) then it will be 256-bits of zeros,
	 * otherwise it will be either 384-bits of zeros (if the
	 * "pending_include_address" flag is not set) or 640-bits of zeros
	 * (if that flag is set).
	 */
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	std::vector<uint8_t> send_buffer;
	{
		nano::vectorstream output_stream (send_buffer);
		nano::uint256_union account_zero (0);
		nano::uint128_union balance_zero (0);

		write (output_stream, account_zero.bytes);

		if (!pending_address_only)
		{
			write (output_stream, balance_zero.bytes);
			if (pending_include_address)
			{
				write (output_stream, account_zero.bytes);
			}
		}
	}

	node->logger.debug (nano::log::type::bulk_pull_account_server, "Bulk sending for an account finished");

	auto this_l (shared_from_this ());
	connection->socket->async_write (nano::shared_const_buffer (std::move (send_buffer)), [this_l] (boost::system::error_code const & ec, std::size_t size_a) {
		this_l->complete (ec, size_a);
	});
}

void nano::bulk_pull_account_server::complete (boost::system::error_code const & ec, std::size_t size_a)
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	if (!ec)
	{
		if (pending_address_only)
		{
			debug_assert (size_a == 32);
		}
		else
		{
			if (pending_include_address)
			{
				debug_assert (size_a == 80);
			}
			else
			{
				debug_assert (size_a == 48);
			}
		}

		connection->start ();
	}
	else
	{
		node->logger.debug (nano::log::type::bulk_pull_account_server, "Unable to pending-as-zero: {}", ec.message ());
	}
}

nano::bulk_pull_account_server::bulk_pull_account_server (std::shared_ptr<nano::transport::tcp_server> const & connection_a, std::unique_ptr<nano::bulk_pull_account> request_a) :
	connection (connection_a),
	request (std::move (request_a)),
	current_key (0, 0)
{
	/*
	 * Setup the streaming response for the first call to "send_frontier" and  "send_next_block"
	 */
	set_params ();
}
