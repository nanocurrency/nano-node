#include <nano/test_common/common.hpp>
#include <nano/test_common/shared_values.hpp>

bool nano::test::shared_gate::wait (std::chrono::milliseconds timeout) const
{
	// Stretched like every test deadline, so a slow run that still meets them does not give up here first
	auto const scaled_timeout = std::chrono::duration_cast<std::chrono::milliseconds> (timeout * nano::test::deadline_scaling_factor ());

	nano::unique_lock<nano::mutex> lock{ gate->mutex };

	// Counted while blocked, so the test can tell that a callback has reached the gate
	++gate->waiting;
	bool const opened = gate->condition.wait_for (lock, scaled_timeout, [this] () {
		return gate->opened;
	});
	--gate->waiting;

	return opened;
}

void nano::test::shared_gate::open () const
{
	{
		nano::lock_guard<nano::mutex> guard{ gate->mutex };
		gate->opened = true;
	}
	gate->condition.notify_all ();
}

std::size_t nano::test::shared_gate::waiting () const
{
	nano::lock_guard<nano::mutex> guard{ gate->mutex };
	return gate->waiting;
}
