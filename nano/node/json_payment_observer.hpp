#pragma once

#include <nano/node/node_observers.hpp>

#include <string>

namespace nano
{
class node;

enum class payment_status
{
	not_a_status,
	unknown,
	nothing, // Timeout and nothing was received
	//insufficient, // Timeout and not enough was received
	//over, // More than requested received
	//success_fork, // Amount received but it involved a fork
	success // Amount received
};
class json_payment_observer final : public std::enable_shared_from_this<nano::json_payment_observer>
{
public:
	json_payment_observer (nano::node &, std::function<void(std::string const &)> const &, nano::account const &, nano::amount const &);
	void start (uint64_t);
	void observe ();
	void complete (nano::payment_status);
	std::mutex mutex;
	nano::condition_variable condition;
	nano::node & node;
	nano::account account;
	nano::amount amount;
	std::function<void(std::string const &)> response;
	std::atomic_flag completed;
};
}
