#pragma once

#include <nano/lib/thread_pool.hpp>
#include <nano/node/fwd.hpp>

namespace nano
{
class http_callbacks
{
public:
	explicit http_callbacks (nano::node &);

	void start ();
	void stop ();

	nano::container_info container_info () const;

private: // Dependencies
	nano::node_config const & config;
	nano::node & node;
	nano::node_observers & observers;
	nano::ledger & ledger;
	nano::logger & logger;
	nano::stats & stats;

private:
	void setup_callbacks ();
	void do_rpc_callback (boost::asio::ip::tcp::resolver::results_type::iterator i_a, boost::asio::ip::tcp::resolver::results_type::iterator end_a, std::string const &, uint16_t, std::shared_ptr<std::string> const &, std::shared_ptr<std::string> const &, std::shared_ptr<boost::asio::ip::tcp::resolver> const &, std::shared_ptr<boost::asio::ip::tcp::resolver::results_type> const & results);

	nano::thread_pool workers;
	nano::interval_mt warning_interval;
};
}