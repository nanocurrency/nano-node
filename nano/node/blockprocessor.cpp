#include <cassert>
#include <nano/lib/timer.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/blockstore.hpp>

std::chrono::milliseconds constexpr nano::block_processor::confirmation_request_delay;

nano::block_processor::block_processor (nano::node & node_a) :
generator (node_a, nano::is_test_network ? std::chrono::milliseconds (10) : std::chrono::milliseconds (500)),
stopped (false),
active (false),
next_log (std::chrono::steady_clock::now ()),
node (node_a)
{
}

nano::block_processor::~block_processor ()
{
	stop ();
}

void nano::block_processor::stop ()
{
	generator.stop ();
	{
		std::lock_guard<std::mutex> lock (mutex);
		stopped = true;
	}
	condition.notify_all ();
}

void nano::block_processor::flush ()
{
	node.checker.flush ();
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped && (have_blocks () || active))
	{
		condition.wait (lock);
	}
}

bool nano::block_processor::full ()
{
	size_t full_size (node.flags.fast_bootstrap ? 1024 * 1024 : 65536);
	std::unique_lock<std::mutex> lock (mutex);
	return (blocks.size () + state_blocks.size ()) > full_size;
}

void nano::block_processor::add (std::shared_ptr<nano::block> block_a, uint64_t origination)
{
	nano::unchecked_info info (block_a, 0, origination, nano::signature_verification::unknown);
	add (info);
}

void nano::block_processor::add (nano::unchecked_info const & info_a)
{
	if (!nano::work_validate (info_a.block->root (), info_a.block->block_work ()))
	{
		{
			auto hash (info_a.block->hash ());
			std::lock_guard<std::mutex> lock (mutex);
			if (blocks_hashes.find (hash) == blocks_hashes.end () && rolled_back.get<1> ().find (hash) == rolled_back.get<1> ().end ())
			{
				if (info_a.verified == nano::signature_verification::unknown && (info_a.block->type () == nano::block_type::state || info_a.block->type () == nano::block_type::open || !info_a.account.is_zero ()))
				{
					state_blocks.push_back (info_a);
				}
				else
				{
					blocks.push_back (info_a);
				}
				blocks_hashes.insert (hash);
			}
		}
		condition.notify_all ();
	}
	else
	{
		BOOST_LOG (node.log) << "nano::block_processor::add called for hash " << info_a.block->hash ().to_string () << " with invalid work " << nano::to_string_hex (info_a.block->block_work ());
		assert (false && "nano::block_processor::add called with invalid work");
	}
}

void nano::block_processor::force (std::shared_ptr<nano::block> block_a)
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		forced.push_back (block_a);
	}
	condition.notify_all ();
}

void nano::block_processor::process_blocks ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped)
	{
		if (have_blocks ())
		{
			active = true;
			lock.unlock ();
			process_batch (lock);
			lock.lock ();
			active = false;
		}
		else
		{
			condition.notify_all ();
			condition.wait (lock);
		}
	}
}

bool nano::block_processor::should_log (bool first_time)
{
	auto result (false);
	auto now (std::chrono::steady_clock::now ());
	if (first_time || next_log < now)
	{
		next_log = now + std::chrono::seconds (15);
		result = true;
	}
	return result;
}

bool nano::block_processor::have_blocks ()
{
	assert (!mutex.try_lock ());
	return !blocks.empty () || !forced.empty () || !state_blocks.empty ();
}

void nano::block_processor::verify_state_blocks (nano::transaction const & transaction_a, std::unique_lock<std::mutex> & lock_a, size_t max_count)
{
	assert (!mutex.try_lock ());
	nano::timer<std::chrono::milliseconds> timer_l (nano::timer_state::started);
	std::deque<nano::unchecked_info> items;
	for (auto i (0); i < max_count && !state_blocks.empty (); i++)
	{
		auto item (state_blocks.front ());
		state_blocks.pop_front ();
		if (!node.ledger.store.block_exists (transaction_a, item.block->type (), item.block->hash ()))
		{
			items.push_back (item);
		}
	}
	lock_a.unlock ();
	if (!items.empty ())
	{
		auto size (items.size ());
		std::vector<nano::uint256_union> hashes;
		hashes.reserve (size);
		std::vector<unsigned char const *> messages;
		messages.reserve (size);
		std::vector<size_t> lengths;
		lengths.reserve (size);
		std::vector<nano::account> accounts;
		accounts.reserve (size);
		std::vector<unsigned char const *> pub_keys;
		pub_keys.reserve (size);
		std::vector<nano::uint512_union> blocks_signatures;
		blocks_signatures.reserve (size);
		std::vector<unsigned char const *> signatures;
		signatures.reserve (size);
		std::vector<int> verifications;
		verifications.resize (size, 0);
		for (auto i (0); i < size; ++i)
		{
			auto item (items[i]);
			hashes.push_back (item.block->hash ());
			messages.push_back (hashes.back ().bytes.data ());
			lengths.push_back (sizeof (decltype (hashes)::value_type));
			nano::account account (item.block->account ());
			if (!item.block->link ().is_zero () && node.ledger.is_epoch_link (item.block->link ()))
			{
				account = node.ledger.epoch_signer;
			}
			else if (!item.account.is_zero ())
			{
				account = item.account;
			}
			accounts.push_back (account);
			pub_keys.push_back (accounts.back ().bytes.data ());
			blocks_signatures.push_back (item.block->block_signature ());
			signatures.push_back (blocks_signatures.back ().bytes.data ());
		}
		nano::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data () };
		node.checker.verify (check);
		lock_a.lock ();
		for (auto i (0); i < size; ++i)
		{
			assert (verifications[i] == 1 || verifications[i] == 0);
			auto item (items.front ());
			if (!item.block->link ().is_zero () && node.ledger.is_epoch_link (item.block->link ()))
			{
				// Epoch blocks
				if (verifications[i] == 1)
				{
					item.verified = nano::signature_verification::valid_epoch;
					blocks.push_back (item);
				}
				else
				{
					// Possible regular state blocks with epoch link (send subtype)
					item.verified = nano::signature_verification::unknown;
					blocks.push_back (item);
				}
			}
			else if (verifications[i] == 1)
			{
				// Non epoch blocks
				item.verified = nano::signature_verification::valid;
				blocks.push_back (item);
			}
			items.pop_front ();
		}
		if (node.config.logging.timing_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Batch verified %1% state blocks in %2% %3%") % size % timer_l.stop ().count () % timer_l.unit ());
		}
	}
	else
	{
		lock_a.lock ();
	}
}

void nano::block_processor::process_batch (std::unique_lock<std::mutex> & lock_a)
{
	nano::timer<std::chrono::milliseconds> timer_l;
	lock_a.lock ();
	timer_l.start ();
	// Limit state blocks verification time
	size_t max_verification_batch (node.flags.fast_bootstrap ? std::numeric_limits<size_t>::max () : 2048 * (node.config.signature_checker_threads + 1));
	if (!state_blocks.empty ())
	{
		auto transaction (node.store.tx_begin_read ());
		while (!state_blocks.empty () && timer_l.before_deadline (std::chrono::seconds (2)))
		{
			verify_state_blocks (transaction, lock_a, max_verification_batch);
		}
	}
	lock_a.unlock ();
	auto transaction (node.store.tx_begin_write ());
	timer_l.restart ();
	lock_a.lock ();
	// Processing blocks
	auto first_time (true);
	unsigned number_of_blocks_processed (0), number_of_forced_processed (0);
	while ((!blocks.empty () || !forced.empty ()) && (timer_l.before_deadline (node.config.block_processor_batch_max_time) || (node.flags.fast_bootstrap && number_of_blocks_processed < 256 * 1024)))
	{
		auto log_this_record (false);
		if (node.config.logging.timing_logging ())
		{
			if (should_log (first_time))
			{
				log_this_record = true;
			}
		}
		else
		{
			if (((blocks.size () + state_blocks.size () + forced.size ()) > 64 && should_log (false)))
			{
				log_this_record = true;
			}
		}

		if (log_this_record)
		{
			first_time = false;
			BOOST_LOG (node.log) << boost::str (boost::format ("%1% blocks (+ %2% state blocks) (+ %3% forced) in processing queue") % blocks.size () % state_blocks.size () % forced.size ());
		}
		nano::unchecked_info info;
		bool force (false);
		if (forced.empty ())
		{
			info = blocks.front ();
			blocks.pop_front ();
			blocks_hashes.erase (info.block->hash ());
		}
		else
		{
			info = nano::unchecked_info (forced.front (), 0, nano::seconds_since_epoch (), nano::signature_verification::unknown);
			forced.pop_front ();
			force = true;
			number_of_forced_processed++;
		}
		lock_a.unlock ();
		auto hash (info.block->hash ());
		if (force)
		{
			auto successor (node.ledger.successor (transaction, nano::uint512_union (info.block->previous (), info.block->root ())));
			if (successor != nullptr && successor->hash () != hash)
			{
				// Replace our block with the winner and roll back any dependent blocks
				BOOST_LOG (node.log) << boost::str (boost::format ("Rolling back %1% and replacing with %2%") % successor->hash ().to_string () % hash.to_string ());
				std::vector<nano::block_hash> rollback_list;
				node.ledger.rollback (transaction, successor->hash (), rollback_list);
				BOOST_LOG (node.log) << boost::str (boost::format ("%1% blocks rolled back") % rollback_list.size ());
				lock_a.lock ();
				// Prevent rolled back blocks second insertion
				auto inserted (rolled_back.insert (nano::rolled_hash{ std::chrono::steady_clock::now (), successor->hash () }));
				if (inserted.second)
				{
					// Possible election winner change
					rolled_back.get<1> ().erase (hash);
					// Prevent overflow
					if (rolled_back.size () > rolled_back_max)
					{
						rolled_back.erase (rolled_back.begin ());
					}
				}
				lock_a.unlock ();
				// Deleting from votes cache
				for (auto & i : rollback_list)
				{
					node.votes_cache.remove (i);
				}
			}
		}
		number_of_blocks_processed++;
		auto process_result (process_one (transaction, info));
		(void)process_result;
		lock_a.lock ();
		/* Verify more state blocks if blocks deque is empty
		 Because verification is long process, avoid large deque verification inside of write transaction */
		if (blocks.empty () && !state_blocks.empty ())
		{
			verify_state_blocks (transaction, lock_a, 256 * (node.config.signature_checker_threads + 1));
		}
	}
	lock_a.unlock ();

	if (node.config.logging.timing_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Processed %1% blocks (%2% blocks were forced) in %3% %4%") % number_of_blocks_processed % number_of_forced_processed % timer_l.stop ().count () % timer_l.unit ());
	}
}

void nano::block_processor::process_live (nano::block_hash const & hash_a, std::shared_ptr<nano::block> block_a)
{
	// Start collecting quorum on block
	node.active.start (block_a);
	// Announce block contents to the network
	node.network.republish_block (block_a);
	if (node.config.enable_voting)
	{
		// Announce our weighted vote to the network
		generator.add (hash_a);
	}
	// Request confirmation for new block with delay
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + confirmation_request_delay, [node_w, block_a]() {
		if (auto node_l = node_w.lock ())
		{
			// Check if votes were already requested
			bool send_request (false);
			{
				std::lock_guard<std::mutex> lock (node_l->active.mutex);
				auto existing (node_l->active.blocks.find (block_a->hash ()));
				if (existing != node_l->active.blocks.end () && !existing->second->confirmed && !existing->second->stopped && existing->second->announcements == 0)
				{
					send_request = true;
				}
			}
			// Request votes
			if (send_request)
			{
				node_l->network.broadcast_confirm_req (block_a);
			}
		}
	});
}

nano::process_return nano::block_processor::process_one (nano::transaction const & transaction_a, nano::unchecked_info info_a)
{
	nano::process_return result;
	auto hash (info_a.block->hash ());
	result = node.ledger.process (transaction_a, *(info_a.block), info_a.verified);
	switch (result.code)
	{
		case nano::process_result::progress:
		{
			release_assert (info_a.account.is_zero () || info_a.account == result.account);
			if (node.config.logging.ledger_logging ())
			{
				std::string block;
				info_a.block->serialize_json (block);
				BOOST_LOG (node.log) << boost::str (boost::format ("Processing block %1%: %2%") % hash.to_string () % block);
			}
			if (info_a.modified > nano::seconds_since_epoch () - 300 && node.block_arrival.recent (hash))
			{
				process_live (hash, info_a.block);
			}
			queue_unchecked (transaction_a, hash);
			break;
		}
		case nano::process_result::gap_previous:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Gap previous for: %1%") % hash.to_string ());
			}
			info_a.verified = result.verified;
			if (info_a.modified == 0)
			{
				info_a.modified = nano::seconds_since_epoch ();
			}
			node.store.unchecked_put (transaction_a, nano::unchecked_key (info_a.block->previous (), hash), info_a);
			node.gap_cache.add (transaction_a, hash);
			break;
		}
		case nano::process_result::gap_source:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Gap source for: %1%") % hash.to_string ());
			}
			info_a.verified = result.verified;
			if (info_a.modified == 0)
			{
				info_a.modified = nano::seconds_since_epoch ();
			}
			node.store.unchecked_put (transaction_a, nano::unchecked_key (node.ledger.block_source (transaction_a, *(info_a.block)), hash), info_a);
			node.gap_cache.add (transaction_a, hash);
			break;
		}
		case nano::process_result::old:
		{
			if (node.config.logging.ledger_duplicate_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Old for: %1%") % hash.to_string ());
			}
			if (!node.flags.fast_bootstrap)
			{
				queue_unchecked (transaction_a, hash);
			}
			node.active.update_difficulty (*(info_a.block));
			break;
		}
		case nano::process_result::bad_signature:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Bad signature for: %1%") % hash.to_string ());
			}
			break;
		}
		case nano::process_result::negative_spend:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Negative spend for: %1%") % hash.to_string ());
			}
			break;
		}
		case nano::process_result::unreceivable:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Unreceivable for: %1%") % hash.to_string ());
			}
			break;
		}
		case nano::process_result::fork:
		{
			if (info_a.modified < nano::seconds_since_epoch () - 15)
			{
				// Only let the bootstrap attempt know about forked blocks that not originate recently.
				node.process_fork (transaction_a, info_a.block);
				node.stats.inc (nano::stat::type::ledger, nano::stat::detail::fork, nano::stat::dir::in);
			}
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Fork for: %1% root: %2%") % hash.to_string () % info_a.block->root ().to_string ());
			}
			break;
		}
		case nano::process_result::opened_burn_account:
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("*** Rejecting open block for burn account ***: %1%") % hash.to_string ());
			break;
		}
		case nano::process_result::balance_mismatch:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Balance mismatch for: %1%") % hash.to_string ());
			}
			break;
		}
		case nano::process_result::representative_mismatch:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Representative mismatch for: %1%") % hash.to_string ());
			}
			break;
		}
		case nano::process_result::block_position:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Block %1% cannot follow predecessor %2%") % hash.to_string () % info_a.block->previous ().to_string ());
			}
			break;
		}
	}
	return result;
}

nano::process_return nano::block_processor::process_one (nano::transaction const & transaction_a, std::shared_ptr<nano::block> block_a)
{
	nano::unchecked_info info (block_a, block_a->account (), 0, nano::signature_verification::unknown);
	auto result (process_one (transaction_a, info));
	return result;
}

void nano::block_processor::queue_unchecked (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto unchecked_blocks (node.store.unchecked_get (transaction_a, hash_a));
	for (auto & info : unchecked_blocks)
	{
		if (!node.flags.fast_bootstrap)
		{
			node.store.unchecked_del (transaction_a, nano::unchecked_key (hash_a, info.block->hash ()));
		}
		add (info);
	}
	std::lock_guard<std::mutex> lock (node.gap_cache.mutex);
	node.gap_cache.blocks.get<1> ().erase (hash_a);
}
