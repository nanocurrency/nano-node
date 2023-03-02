#include <nano/node/blocking_observer.hpp>
#include <nano/node/blockprocessor.hpp>

void nano::blocking_observer::connect (nano::block_processor & block_processor)
{
	block_processor.batch_processed.add ([this] (auto const & items) {
		for (auto const & [result, block] : items)
		{
			observe (result, block);
		}
	});
}

void nano::blocking_observer::stop ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	stopped = true;
	auto discard = std::move (blocking);
	// Signal broken promises outside lock
	lock.unlock ();
	discard.clear (); // ~promise future_error
}

void nano::blocking_observer::observe (nano::process_return const & result, std::shared_ptr<nano::block> block)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	auto existing = blocking.find (block);
	if (existing != blocking.end ())
	{
		auto promise = std::move (existing->second);
		blocking.erase (existing);
		// Signal promise outside of lock
		lock.unlock ();
		promise.set_value (result);
	}
}

std::future<nano::process_return> nano::blocking_observer::insert (std::shared_ptr<nano::block> block)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	if (stopped)
	{
		std::promise<nano::process_return> promise;
		return promise.get_future (); // ~promise future_error
	}
	auto iterator = blocking.emplace (block, std::promise<nano::process_return>{});
	return iterator->second.get_future ();
}

bool nano::blocking_observer::exists (std::shared_ptr<nano::block> block)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto existing = blocking.find (block);
	return existing != blocking.end ();
}
