#pragma once

namespace celerix::secure
{
class transaction;
}

namespace celerix
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
	process_live_dispatcher (celerix::ledger &, celerix::scheduler::priority &, celerix::vote_cache &, celerix::websocket_server &);
	void connect (celerix::block_processor & block_processor);

private:
	// Block_processor observer
	void inspect (celerix::block_status const & result, celerix::block const & block, secure::transaction const & transaction);
	void process_live (celerix::block const & block, secure::transaction const & transaction);

	celerix::ledger & ledger;
	celerix::scheduler::priority & scheduler;
	celerix::vote_cache & vote_cache;
	celerix::websocket_server & websocket;
};
}
