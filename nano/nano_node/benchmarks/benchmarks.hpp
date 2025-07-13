#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/common.hpp>

#include <boost/program_options.hpp>

#include <atomic>
#include <memory>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nano::cli
{
enum class cementing_mode
{
	sequential,
	root
};

class account_pool
{
private:
	std::vector<nano::keypair> keys;
	std::unordered_map<nano::account, nano::keypair> account_to_keypair;
	std::unordered_map<nano::account, nano::uint128_t> balances;
	std::vector<nano::account> accounts_with_balance;
	std::unordered_set<nano::account> balance_lookup;
	std::unordered_map<nano::account, nano::block_hash> frontiers;
	std::random_device rd;
	std::mt19937 gen;

public:
	account_pool ();

	void generate_accounts (size_t count);
	nano::account get_random_account_with_balance ();
	nano::account get_random_account ();
	nano::keypair const & get_keypair (nano::account const & account);
	void update_balance (nano::account const & account, nano::uint128_t new_balance);
	nano::uint128_t get_balance (nano::account const & account);
	bool has_balance (nano::account const & account);
	size_t accounts_with_balance_count () const;
	size_t total_accounts () const;
	std::vector<nano::account> get_accounts_with_balance () const;
	void set_initial_balance (nano::account const & account, nano::uint128_t balance);
	void set_frontier (nano::account const & account, nano::block_hash const & frontier);
	nano::block_hash get_frontier (nano::account const & account) const;
};

struct benchmark_config
{
	size_t num_accounts{ 150000 };
	size_t num_iterations{ 5 };
	size_t batch_size{ 250000 };
	nano::cli::cementing_mode cementing_mode{ nano::cli::cementing_mode::sequential };

	static benchmark_config parse (boost::program_options::variables_map const & vm);
};

class benchmark_base
{
protected:
	account_pool pool;
	std::shared_ptr<nano::node> node;
	benchmark_config config;

	// Common metrics
	std::atomic<size_t> processed_blocks_count{ 0 };

public:
	benchmark_base (std::shared_ptr<nano::node> node_a, benchmark_config const & config_a);
	virtual ~benchmark_base () = default;

	// Transfers genesis balance to a random account to prepare for benchmarking
	void setup_genesis_distribution (double distribution_percentage = 1.0);

	// Generates random transfer pairs between accounts with no specific dependency structure
	std::deque<std::shared_ptr<nano::block>> generate_random_transfers ();

	// Generates blocks that are dependencies of a single root block (last in deque)
	std::deque<std::shared_ptr<nano::block>> generate_dependent_chain ();

	// Generates independent blocks - returns sends and opens separately
	std::pair<std::deque<std::shared_ptr<nano::block>>, std::deque<std::shared_ptr<nano::block>>> generate_independent_blocks ();
};

// Benchmark entry points - individual implementations are in separate cpp files
void run_block_processing_benchmark (boost::program_options::variables_map const & vm, std::filesystem::path const & data_path);
void run_cementing_benchmark (boost::program_options::variables_map const & vm, std::filesystem::path const & data_path);
void run_elections_benchmark (boost::program_options::variables_map const & vm, std::filesystem::path const & data_path);
void run_pipeline_benchmark (boost::program_options::variables_map const & vm, std::filesystem::path const & data_path);
}