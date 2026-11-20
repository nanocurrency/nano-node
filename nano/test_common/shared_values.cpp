#include <nano/test_common/shared_values.hpp>

bool nano::test::shared_gate::wait (std::chrono::milliseconds timeout) const
{
	nano::unique_lock<nano::mutex> lock{ gate->mutex };

	// Counted while blocked, so the test can tell that a callback has reached the gate
	++gate->waiting;
	bool const opened = gate->condition.wait_for (lock, timeout, [this] () {
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
