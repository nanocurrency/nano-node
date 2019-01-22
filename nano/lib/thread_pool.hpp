#pragma once

#include <future>
#include <queue>
#include <thread>

template <typename T>
class thread_pool
{
public:
	using task = std::function<T ()>;
	using packaged_task = std::packaged_task<T ()>;

	thread_pool () :
	thread_pool (std::thread::hardware_concurrency ())
	{
	}

	thread_pool (unsigned int nr_threads) :
	enabled (true)
	{
		threads.reserve (nr_threads);

		for (unsigned int thread_id = 0; thread_id < nr_threads; ++thread_id)
		{
			threads.emplace_back ([&]() {
				while (true)
				{
					packaged_task task;
					{
						std::unique_lock<std::mutex> guard (queue_lock);
						queue_cond.wait (guard, [&] {
							return !enabled || !tasks.empty ();
						});

						if (!enabled && tasks.empty ())
							return;

						// Move the task out of the queue and pop the work queue
						task = std::move (tasks.front ());
						tasks.pop ();
					}

					// Run the actual task
					task ();
				}
			});
		}
	}
	~thread_pool ()
	{
		enabled = false;

		queue_cond.notify_all ();
		for (auto & worker : threads)
			worker.join ();
	}

	template <typename Function>
	std::shared_future<T> submit (Function && func)
	{
		return async_task (std::forward<Function> (func));
	}

private:
	std::shared_future<T> async_task (task task)
	{
		packaged_task packaged_task (std::move (task));
		auto future = packaged_task.get_future ();
		{
			std::unique_lock<std::mutex> guard (queue_lock);
			tasks.push (std::move (packaged_task));
		}

		queue_cond.notify_one ();
		return future.share ();
	}

	std::atomic_bool enabled;
	std::vector<std::thread> threads;
	std::queue<packaged_task> tasks;
	std::mutex queue_lock;
	std::condition_variable queue_cond;
};
