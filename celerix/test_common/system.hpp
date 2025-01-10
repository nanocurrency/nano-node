#pragma once

#include <celerix/lib/errors.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/node/node.hpp>

#include <chrono>
#include <optional>

namespace celerix
{
/** Test-system related error codes */
enum class error_system
{
	generic = 1,
	deadline_expired
};

namespace test
{
	class system final
	{
	public:
		system ();
		system (uint16_t, celerix::transport::transport_type = celerix::transport::transport_type::tcp, celerix::node_flags = celerix::node_flags ());
		~system ();

		void stop ();

		void set_initialization_blocks (std::deque<std::shared_ptr<celerix::block>> blocks);
		void set_cemented_initialization_blocks (std::deque<std::shared_ptr<celerix::block>> blocks);

		void ledger_initialization_set (std::deque<celerix::keypair> const & reps, celerix::amount const & reserve = 0);
		void generate_activity (celerix::node &, std::vector<celerix::account> &);
		void generate_mass_activity (uint32_t, celerix::node &);
		void generate_usage_traffic (uint32_t, uint32_t, size_t);
		void generate_usage_traffic (uint32_t, uint32_t);
		celerix::account get_random_account (std::vector<celerix::account> &);
		celerix::uint128_t get_random_amount (secure::transaction const &, celerix::node &, celerix::account const &);
		void generate_rollback (celerix::node &, std::vector<celerix::account> &);
		void generate_change_known (celerix::node &, std::vector<celerix::account> &);
		void generate_change_unknown (celerix::node &, std::vector<celerix::account> &);
		void generate_receive (celerix::node &);
		void generate_send_new (celerix::node &, std::vector<celerix::account> &);
		void generate_send_existing (celerix::node &, std::vector<celerix::account> &);
		std::shared_ptr<celerix::state_block> upgrade_genesis_epoch (celerix::node &, celerix::epoch const);
		std::shared_ptr<celerix::wallet> wallet (size_t);
		celerix::account account (store::transaction const &, size_t);
		/** Generate work with difficulty between \p min_difficulty_a (inclusive) and \p max_difficulty_a (exclusive) */
		uint64_t work_generate_limited (celerix::block_hash const & root_a, uint64_t min_difficulty_a, uint64_t max_difficulty_a);
		/**
		 * Polls, sleep if there's no work to be done (default 10ms), then check the deadline
		 * @returns 0 or celerix::deadline_expired
		 */
		std::error_code poll (std::chrono::celerixseconds const & sleep_time = std::chrono::milliseconds (10));
		std::error_code poll_until_true (std::chrono::celerixseconds deadline, std::function<bool ()>);
		void delay_ms (std::chrono::milliseconds const & delay);
		void deadline_set (std::chrono::duration<double, std::celerix> const & delta);
		/*
		 * Convenience function to get a reference to a node at given index. Does bound checking.
		 */
		celerix::node & node (std::size_t index) const;
		std::shared_ptr<celerix::node> add_node (celerix::node_flags = celerix::node_flags (), celerix::transport::transport_type = celerix::transport::transport_type::tcp);
		std::shared_ptr<celerix::node> add_node (celerix::node_config const &, celerix::node_flags = celerix::node_flags (), celerix::transport::transport_type = celerix::transport::transport_type::tcp, std::optional<celerix::keypair> const & rep = std::nullopt);

		// Make an independent node that uses system resources but is not part of the system node list and does not automatically connect to other nodes
		std::shared_ptr<celerix::node> make_disconnected_node (std::optional<celerix::node_config> opt_node_config = std::nullopt, celerix::node_flags = celerix::node_flags ());
		void register_node (std::shared_ptr<celerix::node> const &);
		void stop_node (celerix::node &);

		/*
		 * Returns default config for node running in test environment
		 */
		celerix::node_config default_config ();

		/*
		 * Returns port 0 by default, to let the O/S choose a port number.
		 * If CELERIX_TEST_BASE_PORT is set then it allocates numbers by itself from that range.
		 */
		uint16_t get_available_port ();

	private:
		void setup_node (celerix::node &);

	public:
		std::shared_ptr<boost::asio::io_context> io_ctx;
		boost::asio::executor_work_guard<boost::asio::io_context::executor_type> io_guard;
		std::vector<std::shared_ptr<celerix::node>> nodes;
		std::vector<std::shared_ptr<celerix::node>> disconnected_nodes;
		celerix::logger logger;
		celerix::stats stats;
		celerix::work_pool work{ celerix::dev::network_params.network, std::max (celerix::hardware_concurrency (), 1u) };
		std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<double>> deadline{ std::chrono::steady_clock::time_point::max () };
		double deadline_scaling_factor{ 1.0 };
		unsigned node_sequence{ 0 };
		std::deque<std::shared_ptr<celerix::block>> initialization_blocks;
		std::deque<std::shared_ptr<celerix::block>> initialization_blocks_cemented;
	};

	std::shared_ptr<celerix::state_block> upgrade_epoch (celerix::work_pool &, celerix::ledger &, celerix::epoch);
	void cleanup_dev_directories_on_exit ();
}
}
REGISTER_ERROR_CODES (celerix, error_system);
