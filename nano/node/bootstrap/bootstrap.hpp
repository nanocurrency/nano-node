#pragma once

#include "nano/lib/locks.hpp" // for mutex, condition_vari...
#include "nano/lib/numbers.hpp" // for account, block_hash

#include <nano/node/common.hpp> // for endpoint

#include <boost/multi_index/hashed_index.hpp> // for hashed_unique
#include <boost/multi_index/identity_fwd.hpp> // for multi_index
#include <boost/multi_index/indexed_by.hpp> // for indexed_by
#include <boost/multi_index/member.hpp> // for member
#include <boost/multi_index/ordered_index.hpp> // for ordered_non_unique
#include <boost/multi_index/tag.hpp> // for tag
#include <boost/multi_index_container.hpp> // for multi_index_container
#include <boost/thread/detail/thread.hpp> // for thread

#include <atomic> // for atomic
#include <chrono> // for seconds, steady_clock
#include <deque> // for deque
#include <limits> // for numeric_limits
#include <map> // for map
#include <memory> // for allocator, unique_ptr
#include <string> // for string
#include <vector> // for vector

#include <bits/shared_ptr.h> // for shared_ptr
#include <bits/std_function.h> // for function
#include <bits/stdint-uintn.h> // for uint64_t, uint32_t
#include <stddef.h> // for size_t
namespace nano
{
class bootstrap_attempt;
}
namespace nano
{
class container_info_component;
}
namespace nano
{
class pull_info;
}

namespace mi = boost::multi_index;

namespace nano
{
class node;

class bootstrap_connections;
namespace transport
{
	class channel_tcp;
}
enum class bootstrap_mode
{
	legacy,
	lazy,
	wallet_lazy
};
enum class sync_result
{
	success,
	error,
	fork
};
class cached_pulls final
{
public:
	std::chrono::steady_clock::time_point time;
	nano::uint512_union account_head;
	nano::block_hash new_head;
};
class pulls_cache final
{
public:
	void add (nano::pull_info const &);
	void update_pull (nano::pull_info &);
	void remove (nano::pull_info const &);
	nano::mutex pulls_cache_mutex;
	class account_head_tag
	{
	};
	// clang-format off
	boost::multi_index_container<nano::cached_pulls,
	mi::indexed_by<
		mi::ordered_non_unique<
			mi::member<nano::cached_pulls, std::chrono::steady_clock::time_point, &nano::cached_pulls::time>>,
		mi::hashed_unique<mi::tag<account_head_tag>,
			mi::member<nano::cached_pulls, nano::uint512_union, &nano::cached_pulls::account_head>>>>
	cache;
	// clang-format on
	constexpr static size_t cache_size_max = 10000;
};
class bootstrap_attempts final
{
public:
	void add (std::shared_ptr<nano::bootstrap_attempt>);
	void remove (uint64_t);
	void clear ();
	std::shared_ptr<nano::bootstrap_attempt> find (uint64_t);
	size_t size ();
	std::atomic<uint64_t> incremental{ 0 };
	nano::mutex bootstrap_attempts_mutex;
	std::map<uint64_t, std::shared_ptr<nano::bootstrap_attempt>> attempts;
};

class bootstrap_initiator final
{
public:
	explicit bootstrap_initiator (nano::node &);
	~bootstrap_initiator ();
	void bootstrap (nano::endpoint const &, bool add_to_peers = true, std::string id_a = "");
	void bootstrap (bool force = false, std::string id_a = "", uint32_t const frontiers_age_a = std::numeric_limits<uint32_t>::max (), nano::account const & start_account_a = nano::account (0));
	bool bootstrap_lazy (nano::hash_or_account const &, bool force = false, bool confirmed = true, std::string id_a = "");
	void bootstrap_wallet (std::deque<nano::account> &);
	void run_bootstrap ();
	void lazy_requeue (nano::block_hash const &, nano::block_hash const &, bool);
	void notify_listeners (bool);
	void add_observer (std::function<void (bool)> const &);
	bool in_progress ();
	std::shared_ptr<nano::bootstrap_connections> connections;
	std::shared_ptr<nano::bootstrap_attempt> new_attempt ();
	bool has_new_attempts ();
	void remove_attempt (std::shared_ptr<nano::bootstrap_attempt>);
	std::shared_ptr<nano::bootstrap_attempt> current_attempt ();
	std::shared_ptr<nano::bootstrap_attempt> current_lazy_attempt ();
	std::shared_ptr<nano::bootstrap_attempt> current_wallet_attempt ();
	nano::pulls_cache cache;
	nano::bootstrap_attempts attempts;
	void stop ();

private:
	nano::node & node;
	std::shared_ptr<nano::bootstrap_attempt> find_attempt (nano::bootstrap_mode);
	void stop_attempts ();
	std::vector<std::shared_ptr<nano::bootstrap_attempt>> attempts_list;
	std::atomic<bool> stopped{ false };
	nano::mutex mutex;
	nano::condition_variable condition;
	nano::mutex observers_mutex;
	std::vector<std::function<void (bool)>> observers;
	std::vector<boost::thread> bootstrap_initiator_threads;

	friend std::unique_ptr<container_info_component> collect_container_info (bootstrap_initiator & bootstrap_initiator, std::string const & name);
};

std::unique_ptr<container_info_component> collect_container_info (bootstrap_initiator & bootstrap_initiator, std::string const & name);
class bootstrap_limits final
{
public:
	static constexpr double bootstrap_connection_scale_target_blocks = 10000.0;
	static constexpr double bootstrap_connection_warmup_time_sec = 5.0;
	static constexpr double bootstrap_minimum_blocks_per_sec = 10.0;
	static constexpr double bootstrap_minimum_elapsed_seconds_blockrate = 0.02;
	static constexpr double bootstrap_minimum_frontier_blocks_per_sec = 1000.0;
	static constexpr double bootstrap_minimum_termination_time_sec = 30.0;
	static constexpr unsigned bootstrap_max_new_connections = 32;
	static constexpr unsigned requeued_pulls_limit = 256;
	static constexpr unsigned requeued_pulls_limit_dev = 1;
	static constexpr unsigned requeued_pulls_processed_blocks_factor = 4096;
	static constexpr uint64_t pull_count_per_check = 8 * 1024;
	static constexpr unsigned bulk_push_cost_limit = 200;
	static constexpr std::chrono::seconds lazy_flush_delay_sec = std::chrono::seconds (5);
	static constexpr uint64_t lazy_batch_pull_count_resize_blocks_limit = 4 * 1024 * 1024;
	static constexpr double lazy_batch_pull_count_resize_ratio = 2.0;
	static constexpr size_t lazy_blocks_restart_limit = 1024 * 1024;
};
}
