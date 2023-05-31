#pragma once

#include <nano/lib/locks.hpp>
#include <nano/secure/common.hpp>

#include <future>
#include <memory>
#include <unordered_map>

namespace nano
{
class block;
class block_processor;
// Observer that facilitates a blocking call to block processing which is done asynchronosly by the block_processor
class blocking_observer
{
public:
	void connect (nano::block_processor & block_processor);
	// Stop the observer and trigger broken promise exceptions
	void stop ();
	// Block processor observer
	void observe (nano::process_return const & result, std::shared_ptr<nano::block> block);
	[[nodiscard]] std::future<nano::process_return> insert (std::shared_ptr<nano::block> block);
	bool exists (std::shared_ptr<nano::block> block);
	void erase (std::shared_ptr<nano::block> block);

private:
	std::unordered_multimap<std::shared_ptr<nano::block>, std::promise<nano::process_return>> blocking;
	bool stopped{ false };
	nano::mutex mutex;
};
}
