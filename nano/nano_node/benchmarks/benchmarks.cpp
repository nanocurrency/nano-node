#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/lib/timer.hpp>
#include <nano/nano_node/benchmarks/benchmarks.hpp>
#include <nano/node/cli.hpp>
#include <nano/node/daemonconfig.hpp>

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <iostream>
#include <limits>
#include <set>
#include <thread>

#include <fmt/format.h>

namespace nano::cli
{
account_pool::account_pool () :
	gen (rd ())
{
}

void account_pool::generate_accounts (size_t count)
{
	keys.clear ();
	keys.reserve (count);
	account_to_keypair.clear ();
	balances.clear ();
	accounts_with_balance.clear ();
	balance_lookup.clear ();
	frontiers.clear ();

	for (size_t i = 0; i < count; ++i)
	{
		keys.emplace_back ();
		account_to_keypair[keys[i].pub] = keys[i];
		balances[keys[i].pub] = 0;
	}
}

nano::account account_pool::get_random_account_with_balance ()
{
	debug_assert (!accounts_with_balance.empty ());
	std::uniform_int_distribution<size_t> dist (0, accounts_with_balance.size () - 1);
	return accounts_with_balance[dist (gen)];
}

nano::account account_pool::get_random_account ()
{
	debug_assert (!keys.empty ());
	std::uniform_int_distribution<size_t> dist (0, keys.size () - 1);
	return keys[dist (gen)].pub;
}

nano::keypair const & account_pool::get_keypair (nano::account const & account)
{
	auto it = account_to_keypair.find (account);
	debug_assert (it != account_to_keypair.end ());
	return it->second;
}

void account_pool::update_balance (nano::account const & account, nano::uint128_t new_balance)
{
	auto old_balance = balances[account];
	balances[account] = new_balance;

	bool had_balance = balance_lookup.count (account) > 0;
	bool has_balance_now = new_balance > 0;

	if (!had_balance && has_balance_now)
	{
		// Account gained balance
		accounts_with_balance.push_back (account);
		balance_lookup.insert (account);
	}
	else if (had_balance && !has_balance_now)
	{
		// Account lost balance
		auto it = std::find (accounts_with_balance.begin (), accounts_with_balance.end (), account);
		if (it != accounts_with_balance.end ())
		{
			accounts_with_balance.erase (it);
		}
		balance_lookup.erase (account);
	}
}

nano::uint128_t account_pool::get_balance (nano::account const & account)
{
	auto it = balances.find (account);
	return (it != balances.end ()) ? it->second : 0;
}

bool account_pool::has_balance (nano::account const & account)
{
	return balance_lookup.count (account) > 0;
}

size_t account_pool::accounts_with_balance_count () const
{
	return accounts_with_balance.size ();
}

size_t account_pool::total_accounts () const
{
	return keys.size ();
}

std::vector<nano::account> account_pool::get_accounts_with_balance () const
{
	return accounts_with_balance;
}

void account_pool::set_initial_balance (nano::account const & account, nano::uint128_t balance)
{
	balances[account] = balance;
	if (balance > 0)
	{
		if (balance_lookup.count (account) == 0)
		{
			accounts_with_balance.push_back (account);
			balance_lookup.insert (account);
		}
	}
}

void account_pool::set_frontier (nano::account const & account, nano::block_hash const & frontier)
{
	frontiers[account] = frontier;
}

nano::block_hash account_pool::get_frontier (nano::account const & account) const
{
	auto it = frontiers.find (account);
	return (it != frontiers.end ()) ? it->second : nano::block_hash (0);
}

/*
 *
 */

benchmark_config benchmark_config::parse (boost::program_options::variables_map const & vm)
{
	benchmark_config config;

	if (vm.count ("accounts"))
	{
		config.num_accounts = std::stoull (vm["accounts"].as<std::string> ());
	}
	if (vm.count ("iterations"))
	{
		config.num_iterations = std::stoull (vm["iterations"].as<std::string> ());
	}
	if (vm.count ("batch_size"))
	{
		config.batch_size = std::stoull (vm["batch_size"].as<std::string> ());
	}
	if (vm.count ("cementing_mode"))
	{
		auto mode_str = vm["cementing_mode"].as<std::string> ();
		if (mode_str == "root")
		{
			config.cementing_mode = cementing_mode::root;
		}
		else if (mode_str == "sequential")
		{
			config.cementing_mode = cementing_mode::sequential;
		}
		else
		{
			std::cerr << "Invalid cementing mode: " << mode_str << ". Using default (sequential).\n";
		}
	}
	return config;
}

benchmark_base::benchmark_base (std::shared_ptr<nano::node> node_a, benchmark_config const & config_a) :
	node (node_a), config (config_a)
{
}

/*
 * Prepares the ledger for benchmarking by transferring all genesis funds to a single random account.
 * This creates a clean starting state where:
 * - One account holds all the balance (simulating a funded account)
 * - All other accounts start with zero balance
 * - The funded account can then distribute funds to other accounts during the benchmark
 *
 * Algorithm:
 * 1. Select a random account from the pool to be the initial holder
 * 2. Create a send block from genesis account sending all balance
 * 3. Create an open block for the selected account to receive all funds
 * 4. Process both blocks to establish the initial state
 */
void benchmark_base::setup_genesis_distribution (double distribution_percentage)
{
	std::cout << "Setting up genesis distribution...\n";

	// Get genesis balance and latest block
	nano::block_hash genesis_latest (node->latest (nano::dev::genesis_key.pub));
	nano::uint128_t genesis_balance (std::numeric_limits<nano::uint128_t>::max ());

	// Calculate amount to send using 256-bit arithmetic to avoid precision loss
	nano::uint256_t genesis_balance_256 = genesis_balance;
	nano::uint256_t multiplier = static_cast<nano::uint256_t> (distribution_percentage * 1000000);
	nano::uint256_t send_amount_256 = (genesis_balance_256 * multiplier) / 1000000;
	release_assert (send_amount_256 <= std::numeric_limits<nano::uint128_t>::max (), "send amount overflows uint128_t");
	nano::uint128_t send_amount = static_cast<nano::uint128_t> (send_amount_256);
	nano::uint128_t remaining_balance = genesis_balance - send_amount;

	// Select random account to receive genesis funds
	nano::account target_account = pool.get_random_account ();
	auto & target_keypair = pool.get_keypair (target_account);

	// Create send block from genesis to target account
	nano::block_builder builder;
	auto send = builder.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (genesis_latest)
				.representative (nano::dev::genesis_key.pub)
				.balance (remaining_balance)
				.link (target_account)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (0)
				.build ();

	// Create open block for target account
	auto open = builder.state ()
				.account (target_account)
				.previous (0)
				.representative (target_account)
				.balance (send_amount)
				.link (send->hash ())
				.sign (target_keypair.prv, target_keypair.pub)
				.work (0)
				.build ();

	// Process blocks
	auto result1 = node->process (send);
	release_assert (result1 == nano::block_status::progress, to_string (result1));
	auto result2 = node->process (open);
	release_assert (result2 == nano::block_status::progress, to_string (result2));

	// Update pool balance tracking
	pool.set_initial_balance (target_account, send_amount);

	// Initialize frontier for target account
	pool.set_frontier (target_account, open->hash ());

	std::cout << fmt::format ("Genesis distribution complete: {:.1f}% distributed, {:.1f}% retained for voting\n",
	distribution_percentage * 100.0, (1.0 - distribution_percentage) * 100.0);
}

/*
 * Generates random transfer transactions between accounts with no specific dependency pattern.
 * This simulates typical network activity with independent transactions.
 *
 * Algorithm:
 * 1. For each transfer (batch_size/2 transfers, since each creates 2 blocks):
 *    a. Select a random sender account that has balance
 *    b. Select a random receiver account (can be any account)
 *    c. Generate a random transfer amount (up to sender's balance)
 *    d. Create a send block from sender
 *    e. Create a receive/open block for receiver
 * 2. Update account balances and frontiers after each transfer
 * 3. Continue until batch_size blocks are generated or no accounts have balance
 *
 * The resulting blocks have no intentional dependency structure beyond the natural
 * send->receive pairs, making this suitable for testing sequential block processing.
 */
std::deque<std::shared_ptr<nano::block>> benchmark_base::generate_random_transfers ()
{
	std::deque<std::shared_ptr<nano::block>> blocks;
	std::random_device rd;
	std::mt19937 gen (rd ());

	// Generate batch_size number of transfer pairs (send + receive = 2 blocks each)
	size_t transfers_generated = 0;
	nano::block_builder builder;

	while (transfers_generated < config.batch_size / 2) // Divide by 2 since each transfer creates 2 blocks
	{
		if (pool.accounts_with_balance_count () == 0)
		{
			std::cout << "No accounts with balance remaining, stopping...\n";
			break;
		}

		// Get random sender with balance
		nano::account sender = pool.get_random_account_with_balance ();
		auto & sender_keypair = pool.get_keypair (sender);
		nano::uint128_t sender_balance = pool.get_balance (sender);

		if (sender_balance == 0)
			continue;

		// Get random receiver
		nano::account receiver = pool.get_random_account ();
		auto & receiver_keypair = pool.get_keypair (receiver);

		// Random transfer amount (but not more than sender balance)
		std::uniform_int_distribution<uint64_t> amount_dist (1, sender_balance.convert_to<uint64_t> ());
		nano::uint128_t transfer_amount = std::min (static_cast<nano::uint128_t> (amount_dist (gen)), sender_balance);

		// Get or initialize sender frontier
		nano::block_hash sender_frontier = pool.get_frontier (sender);
		nano::root work_root;
		if (sender_frontier != 0)
		{
			work_root = sender_frontier;
		}
		else
		{
			sender_frontier = 0; // First block for this account
			work_root = sender; // Use account address for first block work
		}

		// Create send block
		nano::uint128_t new_sender_balance = sender_balance - transfer_amount;
		auto send = builder.state ()
					.account (sender)
					.previous (sender_frontier)
					.representative (sender)
					.balance (new_sender_balance)
					.link (receiver)
					.sign (sender_keypair.prv, sender_keypair.pub)
					.work (0)
					.build ();

		blocks.push_back (send);
		pool.set_frontier (sender, send->hash ());
		pool.update_balance (sender, new_sender_balance);

		// Create receive block
		nano::uint128_t receiver_balance = pool.get_balance (receiver);
		nano::uint128_t new_receiver_balance = receiver_balance + transfer_amount;

		nano::block_hash receiver_frontier = pool.get_frontier (receiver);
		nano::root receiver_work_root;
		if (receiver_frontier != 0)
		{
			receiver_work_root = receiver_frontier;
		}
		else
		{
			receiver_frontier = 0; // First block for this account (open block)
			receiver_work_root = receiver; // Use account address for first block work
		}

		auto receive = builder.state ()
					   .account (receiver)
					   .previous (receiver_frontier)
					   .representative (receiver)
					   .balance (new_receiver_balance)
					   .link (send->hash ())
					   .sign (receiver_keypair.prv, receiver_keypair.pub)
					   .work (0)
					   .build ();

		blocks.push_back (receive);
		pool.set_frontier (receiver, receive->hash ());
		pool.update_balance (receiver, new_receiver_balance);

		transfers_generated++;
	}

	std::cout << fmt::format ("Generated {} blocks\n", blocks.size ());

	return blocks;
}

/*
 * Generates blocks in a dependency tree structure optimized for root mode cementing.
 * All blocks are organized so they become dependencies of a single root block.
 *
 * Algorithm:
 * 1. Random transfer phase (80% of blocks):
 *    - Generate random transfers between accounts (same as generate_random_transfers)
 *    - Creates a natural web of dependencies through send/receive pairs
 * 2. Convergence phase (20% of blocks):
 *    - All accounts with balance send their entire balance to a collector account
 *    - The collector receives all these sends in sequence
 *    - The final receive block becomes the root that depends on all previous blocks
 *
 * The last block in the returned deque is the ultimate root that depends on all others.
 * Cementing this single block will cascade and cement all blocks in the tree.
 */
std::deque<std::shared_ptr<nano::block>> benchmark_base::generate_dependent_chain ()
{
	std::deque<std::shared_ptr<nano::block>> blocks;
	std::random_device rd;
	std::mt19937 gen (rd ());
	nano::block_builder builder;

	// Phase 1: Random transfers (80% of blocks)
	size_t random_transfer_blocks = config.batch_size * 0.8;
	size_t transfers_to_generate = random_transfer_blocks / 2; // Each transfer creates 2 blocks

	std::cout << fmt::format ("Generating dependent chain: {} random transfers, then convergence\n",
	transfers_to_generate);

	// Phase 1: Generate random transfers (same logic as generate_random_transfers)
	size_t transfers_generated = 0;
	while (transfers_generated < transfers_to_generate && pool.accounts_with_balance_count () > 0)
	{
		// Get random sender with balance
		nano::account sender = pool.get_random_account_with_balance ();
		auto & sender_keypair = pool.get_keypair (sender);
		nano::uint128_t sender_balance = pool.get_balance (sender);

		if (sender_balance == 0)
			continue;

		// Get random receiver
		nano::account receiver = pool.get_random_account ();
		auto & receiver_keypair = pool.get_keypair (receiver);

		// Random transfer amount (but not more than sender balance)
		std::uniform_int_distribution<uint64_t> amount_dist (1, sender_balance.convert_to<uint64_t> ());
		nano::uint128_t transfer_amount = std::min (static_cast<nano::uint128_t> (amount_dist (gen)), sender_balance);

		// Get or initialize sender frontier
		nano::block_hash sender_frontier = pool.get_frontier (sender);

		// Create send block
		nano::uint128_t new_sender_balance = sender_balance - transfer_amount;
		auto send = builder.state ()
					.account (sender)
					.previous (sender_frontier)
					.representative (sender)
					.balance (new_sender_balance)
					.link (receiver)
					.sign (sender_keypair.prv, sender_keypair.pub)
					.work (0)
					.build ();

		blocks.push_back (send);
		pool.set_frontier (sender, send->hash ());
		pool.update_balance (sender, new_sender_balance);

		// Create receive block
		nano::uint128_t receiver_balance = pool.get_balance (receiver);
		nano::uint128_t new_receiver_balance = receiver_balance + transfer_amount;
		nano::block_hash receiver_frontier = pool.get_frontier (receiver);

		auto receive = builder.state ()
					   .account (receiver)
					   .previous (receiver_frontier)
					   .representative (receiver)
					   .balance (new_receiver_balance)
					   .link (send->hash ())
					   .sign (receiver_keypair.prv, receiver_keypair.pub)
					   .work (0)
					   .build ();

		blocks.push_back (receive);
		pool.set_frontier (receiver, receive->hash ());
		pool.update_balance (receiver, new_receiver_balance);

		transfers_generated++;
	}

	// Phase 2: Convergence - all accounts with balance send to a collector
	std::cout << fmt::format ("Converging {} accounts to collector account\n",
	pool.accounts_with_balance_count ());

	// Select a collector account (can be new or existing)
	nano::account collector = pool.get_random_account ();
	auto & collector_keypair = pool.get_keypair (collector);
	nano::block_hash collector_frontier = pool.get_frontier (collector);
	nano::uint128_t collector_balance = pool.get_balance (collector);

	// Collect all accounts with balance (except collector)
	std::vector<std::pair<nano::account, nano::uint128_t>> accounts_to_drain;
	auto accounts_with_balance = pool.get_accounts_with_balance ();
	for (auto const & account : accounts_with_balance)
	{
		if (account != collector)
		{
			nano::uint128_t balance = pool.get_balance (account);
			accounts_to_drain.push_back ({ account, balance });
		}
	}

	// All accounts send a random amount to collector
	std::vector<std::pair<nano::block_hash, nano::uint128_t>> convergence_sends;
	for (auto const & [account, balance] : accounts_to_drain)
	{
		auto & account_keypair = pool.get_keypair (account);
		nano::block_hash account_frontier = pool.get_frontier (account);

		// Send random amount to collector (between 1 and full balance)
		std::uniform_int_distribution<uint64_t> amount_dist (1, balance.convert_to<uint64_t> ());
		nano::uint128_t send_amount = static_cast<nano::uint128_t> (amount_dist (gen));
		nano::uint128_t remaining_balance = balance - send_amount;

		auto send = builder.state ()
					.account (account)
					.previous (account_frontier)
					.representative (account)
					.balance (remaining_balance)
					.link (collector)
					.sign (account_keypair.prv, account_keypair.pub)
					.work (0)
					.build ();

		blocks.push_back (send);
		convergence_sends.push_back ({ send->hash (), send_amount });
		pool.set_frontier (account, send->hash ());
		pool.update_balance (account, remaining_balance);
	}

	// Collector receives all sends (these become the root blocks)
	for (auto const & [send_hash, amount] : convergence_sends)
	{
		collector_balance += amount;
		auto receive = builder.state ()
					   .account (collector)
					   .previous (collector_frontier)
					   .representative (collector)
					   .balance (collector_balance)
					   .link (send_hash)
					   .sign (collector_keypair.prv, collector_keypair.pub)
					   .work (0)
					   .build ();

		blocks.push_back (receive);
		collector_frontier = receive->hash ();
	}

	// Update collector state
	pool.set_frontier (collector, collector_frontier);
	pool.update_balance (collector, collector_balance);

	std::cout << fmt::format ("Generated {} blocks in dependent chain topology\n", blocks.size ());

	return blocks;
}

/*
 * Generates independent blocks - one block per account with no dependencies.
 * Returns sends and opens separately so sends can be confirmed first, then opens processed for elections.
 */
std::pair<std::deque<std::shared_ptr<nano::block>>, std::deque<std::shared_ptr<nano::block>>> benchmark_base::generate_independent_blocks ()
{
	std::deque<std::shared_ptr<nano::block>> sends;
	std::deque<std::shared_ptr<nano::block>> opens;
	nano::block_builder builder;

	// Find accounts with balance to send from
	auto accounts_with_balance = pool.get_accounts_with_balance ();
	if (accounts_with_balance.empty ())
	{
		std::cout << "No accounts with balance available\n";
		return { sends, opens };
	}

	// Generate independent blocks up to batch_size
	for (size_t i = 0; i < config.batch_size && !accounts_with_balance.empty (); ++i)
	{
		// Pick a sender with balance
		nano::account sender = accounts_with_balance[i % accounts_with_balance.size ()];
		auto & sender_keypair = pool.get_keypair (sender);
		nano::uint128_t sender_balance = pool.get_balance (sender);

		if (sender_balance == 0)
			continue;

		// Create a brand new receiver account
		nano::keypair receiver_keypair;
		nano::account receiver = receiver_keypair.pub;

		// Send a small amount to the new account
		nano::uint128_t transfer_amount = std::min (sender_balance, nano::uint128_t (1000000)); // Small fixed amount
		nano::block_hash sender_frontier = pool.get_frontier (sender);
		nano::uint128_t new_sender_balance = sender_balance - transfer_amount;

		// Create send block
		auto send = builder.state ()
					.account (sender)
					.previous (sender_frontier)
					.representative (sender)
					.balance (new_sender_balance)
					.link (receiver)
					.sign (sender_keypair.prv, sender_keypair.pub)
					.work (0)
					.build ();

		// Create open block for new receiver (this is the independent block)
		auto open = builder.state ()
					.account (receiver)
					.previous (0) // First block for this account
					.representative (receiver)
					.balance (transfer_amount)
					.link (send->hash ())
					.sign (receiver_keypair.prv, receiver_keypair.pub)
					.work (0)
					.build ();

		// Separate sends and opens
		sends.push_back (send);
		opens.push_back (open);

		// Update pool state for sender only (receiver is new account not tracked)
		pool.set_frontier (sender, send->hash ());
		pool.update_balance (sender, new_sender_balance);
	}

	std::cout << fmt::format ("Generated {} sends and {} opens\n", sends.size (), opens.size ());

	return { sends, opens };
}
}