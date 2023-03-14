#pragma once

namespace nano
{
class ledger;
class election_scheduler;
class vote_cache;
class websocket_server;
class block_processor;
class process_return;
class block;
class transaction;

// Observes confirmed blocks and dispatches the process_live function.
class process_live_dispatcher
{
public:
	process_live_dispatcher (nano::ledger & ledger, nano::election_scheduler & scheduler, nano::vote_cache & inactive_vote_cache, nano::websocket_server & websocket);
	void connect (nano::block_processor & block_processor);

private:
	// Block_processor observer
	void inspect (nano::process_return const & result, nano::block const & block, nano::transaction const & transaction);
	void process_live (nano::block const & block, nano::transaction const & transaction);

	nano::ledger & ledger;
	nano::election_scheduler & scheduler;
	nano::vote_cache & inactive_vote_cache;
	nano::websocket_server & websocket;
};
}
