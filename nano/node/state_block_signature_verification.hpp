#pragma once

#include <nano/lib/locks.hpp>
#include <nano/secure/common.hpp>

#include <deque>
#include <functional>
#include <thread>

namespace nano
{
class epochs;
class logger_mt;
class node_config;
class signature_checker;

class state_block_signature_verification
{
public:
	state_block_signature_verification (nano::signature_checker &, nano::epochs &, nano::node_config &, nano::logger_mt &, uint64_t);
	~state_block_signature_verification ();
	void add (nano::unchecked_info const & info_a);
	size_t size ();
	void stop ();
	bool is_active ();

	std::function<void (std::deque<nano::unchecked_info> &, std::vector<int> const &, std::vector<nano::block_hash> const &, std::vector<nano::signature> const &)> blocks_verified_callback;
	std::function<void ()> transition_inactive_callback;

private:
	nano::signature_checker & signature_checker;
	nano::epochs & epochs;
	nano::node_config & node_config;
	nano::logger_mt & logger;

	nano::mutex mutex{ mutex_identifier (mutexes::state_block_signature_verification) };
	bool stopped{ false };
	bool active{ false };
	std::deque<nano::unchecked_info> state_blocks;
	nano::condition_variable condition;
	std::thread thread;

	void run (uint64_t block_processor_verification_size);
	std::deque<nano::unchecked_info> setup_items (size_t);
	void verify_state_blocks (std::deque<nano::unchecked_info> &);
};

std::unique_ptr<nano::container_info_component> collect_container_info (state_block_signature_verification & state_block_signature_verification, std::string const & name);
}
