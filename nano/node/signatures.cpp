#include <nano/boost/asio/post.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/signatures.hpp>

nano::signature_checker::signature_checker (unsigned num_threads) :
	thread_pool (num_threads, nano::thread_role::name::signature_checking)
{
}

nano::signature_checker::~signature_checker ()
{
	stop ();
}

void nano::signature_checker::verify (nano::signature_check_set & check_a)
{
	// Don't process anything else if we have stopped
	if (stopped)
	{
		return;
	}

	if (check_a.size <= batch_size || single_threaded ())
	{
		// Not dealing with many so just use the calling thread for checking signatures
		auto result = verify_batch (check_a, 0, check_a.size);
		release_assert (result);
		return;
	}

	// Split up the tasks equally over the calling thread and the thread pool.
	// Any overflow on the modulus of the batch_size is given to the calling thread, so the thread pool
	// only ever operates on batch_size sizes.
	std::size_t overflow_size = check_a.size % batch_size;
	std::size_t num_full_batches = check_a.size / batch_size;

	auto const num_threads = thread_pool.get_num_threads ();
	auto total_threads_to_split_over = num_threads + 1;
	auto num_base_batches_each = num_full_batches / total_threads_to_split_over;
	auto num_full_overflow_batches = num_full_batches % total_threads_to_split_over;

	auto size_calling_thread = (num_base_batches_each * batch_size) + overflow_size;
	auto num_full_batches_thread = (num_base_batches_each * num_threads);
	if (num_full_overflow_batches > 0)
	{
		if (overflow_size == 0)
		{
			// Give the calling thread priority over any batches when there is no excess remainder.
			size_calling_thread += batch_size;
			num_full_batches_thread += num_full_overflow_batches - 1;
		}
		else
		{
			num_full_batches_thread += num_full_overflow_batches;
		}
	}

	release_assert (check_a.size == (num_full_batches_thread * batch_size + size_calling_thread));

	std::promise<void> promise;
	std::future<void> future = promise.get_future ();

	// Verify a number of signature batches over the thread pool (does not block)
	verify_async (check_a, num_full_batches_thread, promise);

	// Verify the rest on the calling thread, this operates on the signatures at the end of the check set
	auto result = verify_batch (check_a, check_a.size - size_calling_thread, size_calling_thread);
	release_assert (result);

	// Blocks until all the work is done
	future.wait ();
}

void nano::signature_checker::stop ()
{
	if (!stopped.exchange (true))
	{
		thread_pool.stop ();
	}
}

void nano::signature_checker::flush ()
{
	while (!stopped && tasks_remaining != 0)
		;
}

bool nano::signature_checker::verify_batch (nano::signature_check_set const & check_a, std::size_t start_index, std::size_t size)
{
	nano::validate_message_batch (check_a.messages + start_index, check_a.message_lengths + start_index, check_a.pub_keys + start_index, check_a.signatures + start_index, size, check_a.verifications + start_index);
	return std::all_of (check_a.verifications + start_index, check_a.verifications + start_index + size, [] (int verification) { return verification == 0 || verification == 1; });
}

/* This operates on a number of signatures of size (num_batches * batch_size) from the beginning of the check_a pointers.
 * Caller should check the value of the promise which indicates when the work has been completed.
 */
void nano::signature_checker::verify_async (nano::signature_check_set & check_a, std::size_t num_batches, std::promise<void> & promise)
{
	auto task = std::make_shared<Task> (check_a, num_batches);
	++tasks_remaining;

	for (std::size_t batch = 0; batch < num_batches; ++batch)
	{
		auto size = batch_size;
		auto start_index = batch * batch_size;

		thread_pool.push_task ([this, task, size, start_index, &promise] {
			auto result = this->verify_batch (task->check, start_index, size);
			release_assert (result);

			if (--task->pending == 0)
			{
				--tasks_remaining;
				promise.set_value ();
			}
		});
	}
}

bool nano::signature_checker::single_threaded () const
{
	return thread_pool.get_num_threads () == 0;
}
