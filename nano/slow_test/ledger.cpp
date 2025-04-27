#include <nano/lib/config.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/test_common/ledger_context.hpp>
#include <nano/test_common/make_store.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <boost/thread.hpp>

namespace
{
void rollback_degenerate_impl (nano::test::ledger_context & ctx, size_t num_accounts)
{
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto & pool = ctx.pool ();

	std::vector<nano::keypair> keys (num_accounts); // Keys for A0 to A(N-1)

	// Track the head block hash for each account involved (Genesis + A0..A(N-1))
	std::vector<nano::block_hash> current_head_hashes (num_accounts + 1);
	current_head_hashes[0] = nano::dev::genesis->hash (); // Index 0 for Genesis

	// Track balances (needed for creating send blocks)
	std::vector<nano::uint128_t> current_balances (num_accounts + 1);
	current_balances[0] = nano::dev::constants.genesis_amount; // Index 0 for Genesis

	nano::block_hash first_send_hash; // Store the hash of the first send (G->A0)
	nano::block_hash first_receive_hash; // Store the hash of the first receive (A0 open)

	std::cout << "Creating and processing " << num_accounts << " send/receive pairs..." << std::endl;
	{
		auto transaction = ledger.tx_begin_write ();
		for (uint32_t i = 0; i < num_accounts; ++i)
		{
			// Determine sender: Genesis (index 0) for i=0, Ai-1 (index i) for i>0
			uint32_t sender_account_index = i;
			nano::keypair sender_key = (i == 0) ? nano::dev::genesis_key : keys[i - 1];
			nano::block_hash sender_previous_hash = current_head_hashes[sender_account_index];
			nano::uint128_t sender_balance = current_balances[sender_account_index];

			nano::block_builder builder;

			// --- Send Block ---
			sender_balance -= 1; // Send 1 raw
			auto send_block = builder.state ()
							  .account (sender_key.pub)
							  .previous (sender_previous_hash)
							  .representative (sender_key.pub) // Keep self representation
							  .balance (sender_balance)
							  .link (keys[i].pub) // Destination is keys[i] (Ai)
							  .sign (sender_key.prv, sender_key.pub)
							  .work (*pool.generate (sender_previous_hash))
							  .build ();

			ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, send_block)) << "Failed processing send block " << i;
			current_head_hashes[sender_account_index] = send_block->hash (); // Update sender's chain head
			current_balances[sender_account_index] = sender_balance; // Update sender's balance

			// --- Receive Block (acting as open for Ai) ---
			// Receiver is account Ai (keys[i]), index i+1 in our vectors
			uint32_t receiver_account_index = i + 1;
			auto receive_block = builder.state ()
								 .account (keys[i].pub) // Account Ai
								 .previous (0) // Open block
								 .representative (keys[i].pub) // Self representation
								 .balance (1) // Received 1 raw
								 .link (send_block->hash ()) // Link to the send block
								 .sign (keys[i].prv, keys[i].pub)
								 .work (*pool.generate (keys[i].pub))
								 .build ();

			if (i == 0)
			{
				first_send_hash = send_block->hash (); // Capture the first send hash (G->A0)
				first_receive_hash = receive_block->hash (); // Capture the first receive hash (A0 open)
			}

			ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, receive_block)) << "Failed processing receive block " << i;
			current_head_hashes[receiver_account_index] = receive_block->hash (); // Update head for Ai
			current_balances[receiver_account_index] = 1; // Update balance for Ai

			// Commit periodically to keep transaction size manageable and release locks
			if ((i + 1) % 1000 == 0)
			{
				std::cout << "Processed " << (i + 1) << " pairs." << std::endl;
				transaction.commit ();
				transaction.renew ();
			}
		}
		std::cout << "Block processing finished." << std::endl;

		// Final sanity checks before committing the last batch

	} // Commit final transaction batch

	// Verify state after processing
	{
		auto transaction = ledger.tx_begin_read ();

		ASSERT_EQ (ledger.block_count (), 1 + num_accounts * 2);
		ASSERT_EQ (ledger.account_count (), 1 + num_accounts);
		ASSERT_EQ (ledger.any.account_balance (transaction, nano::dev::genesis_key.pub), nano::dev::constants.genesis_amount - 1); // Genesis only sent once to A0
		for (uint32_t i = 0; i < num_accounts; ++i)
		{
			// Account Ai (where i is 0..N-1)
			if (i == num_accounts - 1)
			{ // Last account A(N-1) only received
				ASSERT_EQ (ledger.any.account_balance (transaction, keys[i].pub).value_or (0), 1);
			}
			else
			{ // Accounts A0 to A(N-2) received then sent
				ASSERT_EQ (ledger.any.account_balance (transaction, keys[i].pub).value_or (0), 0);
			}
		}

		auto pending_info = ledger.any.pending_get (transaction, nano::pending_key (keys[0].pub, first_send_hash));
		ASSERT_FALSE (pending_info);
	}

	// Initiate rollback from the *first* send (G->A0)
	std::cout << "Starting degenerate rollback from first send (G->A0)..." << std::endl;
	std::deque<std::shared_ptr<nano::block>> rolled_back_list;
	{
		auto transaction = ledger.tx_begin_write ();
		ASSERT_FALSE (first_receive_hash.is_zero ()); // Make sure we captured it
		bool rollback_error;
		ASSERT_NO_THROW (rollback_error = ledger.rollback (transaction, first_receive_hash, rolled_back_list));
		ASSERT_FALSE (rollback_error); // Expect rollback to succeed
	};
	std::cout << "Rollback finished." << std::endl;

	// Verify final state
	{
		auto transaction = ledger.tx_begin_read ();
		ASSERT_EQ (ledger.block_count (), 2); // Only Genesis chain should remain
		ASSERT_EQ (ledger.account_count (), 1); // Only Genesis account
		ASSERT_EQ (ledger.any.account_balance (transaction, nano::dev::genesis_key.pub), nano::dev::constants.genesis_amount - 1);
		ASSERT_EQ (ledger.weight (nano::dev::genesis_key.pub), nano::dev::constants.genesis_amount - 1); // Unreceived 1 raw send

		// Ensure all derived accounts are gone and have zero weight
		for (const auto & key : keys)
		{
			ASSERT_FALSE (ledger.any.account_exists (transaction, key.pub));
			ASSERT_EQ (ledger.weight (key.pub), 0);
			ASSERT_FALSE (ledger.any.account_balance (transaction, key.pub));
		}

		// Pending check for the first send (G->A0) should be restored
		auto pending_info = ledger.any.pending_get (transaction, nano::pending_key (keys[0].pub, first_send_hash));
		ASSERT_TRUE (pending_info);
		ASSERT_EQ (pending_info->source, nano::dev::genesis_key.pub);
		ASSERT_EQ (pending_info->amount, 1);

		// If rollback completed without stack overflow, all blocks (except first genesis send) should be in the list
		ASSERT_EQ (rolled_back_list.size (), num_accounts * 2 - 1);
	}
	std::cout << "Degenerate interleaved rollback test completed successfully." << std::endl;
}
}

// Test rollback of a long chain involving multiple accounts (A->B->C...),
// with interleaved send/receive processing. Designed to trigger deep recursion in the rollback visitor.
// WARNING: High values for num_accounts can easily cause stack overflows.
// This test verifies the rollback logic handles the recursive structure correctly *within stack limits*.
TEST (ledger, rollback_degenerate)
{
	// Adjust this number based on desired rollback tolerance
	constexpr uint32_t num_accounts = 100000;

	auto ctx = nano::test::ledger_empty ();

	boost::thread::attributes attrs;
	attrs.set_stack_size (nano::ledger_thread_stack_size ());

	boost::thread thread (attrs, [&] () {
		rollback_degenerate_impl (ctx, num_accounts);
	});

	thread.join ();
}