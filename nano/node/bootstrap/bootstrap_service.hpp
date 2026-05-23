#pragma once

#include <nano/messages/fwd.hpp>
#include <nano/node/fwd.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <memory>

namespace nano
{
class bootstrap_service
{
public:
	bootstrap_service (nano::node_config const &, nano::node &, nano::ledger &, nano::ledger_notifications &, nano::block_processor &, nano::network &, nano::stats &, nano::logger &);
	~bootstrap_service ();

	void start ();
	void stop ();

	/**
	 * Process bootstrap messages coming from the network
	 */
	void process (nano::messages::asc_pull_ack const & message, std::shared_ptr<nano::transport::channel> const &);

	/**
	 * Clears priority and blocking accounts state
	 */
	void reset ();

	/**
	 * Adds an account to the priority set
	 */
	void prioritize (nano::account const & account);

	std::size_t blocked_size () const;
	std::size_t priority_size () const;
	std::size_t score_size () const;

	bool prioritized (nano::account const &) const;
	bool blocked (nano::account const &) const;

	boost::property_tree::ptree info () const;

	struct status_result
	{
		size_t priorities;
		size_t blocking;
	};
	status_result status () const;

	nano::container_info container_info () const;

private: // Dependencies
	nano::node_config const & config;
	nano::ledger & ledger;
	nano::ledger_notifications & ledger_notifications;
	nano::block_processor & block_processor;
	nano::network & network;
	nano::stats & stats;
	nano::logger & logger;

private:
	std::unique_ptr<nano::bootstrap::bootstrap_context> ctx_impl;
	nano::bootstrap::bootstrap_context & ctx;
};
}
