#pragma once

namespace nano::store
{
class transaction;
}
namespace nano::voting
{
class cache;
}

namespace nano
{
class ledger;
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
	process_live_dispatcher (nano::ledger &, nano::scheduler::priority &, nano::voting::cache &, nano::websocket_server &);
	void connect (nano::block_processor & block_processor);

private:
	// Block_processor observer
	void inspect (nano::process_return const & result, nano::block const & block, store::transaction const & transaction);
	void process_live (nano::block const & block, store::transaction const & transaction);

	nano::ledger & ledger;
	nano::scheduler::priority & scheduler;
	nano::voting::cache & vote_cache;
	nano::websocket_server & websocket;
};
}
