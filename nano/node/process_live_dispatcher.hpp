#pragma once

namespace nano::secure
{
class transaction;
}

namespace nano
{
class ledger;
class vote_cache;
class websocket_server;
class block_processor;
class process_return;
class block;

namespace scheduler
{
	class priority;
}

// Observes confirmed blocks and dispatches the process_live function.
class process_live_dispatcher
{
public:
	process_live_dispatcher (nano::ledger &, nano::scheduler::priority &, nano::vote_cache &, nano::websocket_server &);
	void connect (nano::block_processor & block_processor);

private:
	// Block_processor observer
	void inspect (nano::block_status const & result, nano::block const & block);
	void process_live (nano::block const & block);

	nano::ledger & ledger;
	nano::scheduler::priority & scheduler;
	nano::vote_cache & vote_cache;
	nano::websocket_server & websocket;
};
}
