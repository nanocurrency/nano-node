#include <nano/lib/work.hpp>

#include <nano/lib/blocks.hpp>
#include <nano/node/xorshift.hpp>

#include <future>

bool nano::work_validate (nano::block_hash const & root_a, uint64_t work_a, uint64_t * difficulty_a)
{
	auto value (nano::work_value (root_a, work_a));
	if (difficulty_a != nullptr)
	{
		*difficulty_a = value;
	}
	return value < nano::work_pool::publish_threshold;
}

bool nano::work_validate (nano::block const & block_a, uint64_t * difficulty_a)
{
	return work_validate (block_a.root (), block_a.block_work (), difficulty_a);
}

uint64_t nano::work_value (nano::block_hash const & root_a, uint64_t work_a)
{
	uint64_t result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result));
	blake2b_update (&hash, reinterpret_cast<uint8_t *> (&work_a), sizeof (work_a));
	blake2b_update (&hash, root_a.bytes.data (), root_a.bytes.size ());
	blake2b_final (&hash, reinterpret_cast<uint8_t *> (&result), sizeof (result));
	return result;
}

nano::work_pool::work_pool (unsigned max_threads_a, std::function<boost::optional<uint64_t> (nano::uint256_union const &)> opencl_a) :
ticket (0),
done (false),
opencl (opencl_a)
{
	static_assert (ATOMIC_INT_LOCK_FREE == 2, "Atomic int needed");
	boost::thread::attributes attrs;
	nano::thread_attributes::set (attrs);
	auto count (nano::is_test_network ? 1 : std::min (max_threads_a, std::max (1u, boost::thread::hardware_concurrency ())));
	for (auto i (0); i < count; ++i)
	{
		auto thread (boost::thread (attrs, [this, i]() {
			nano::thread_role::set (nano::thread_role::name::work);
			nano::work_thread_reprioritize ();
			loop (i);
		}));
		threads.push_back (std::move (thread));
	}
}

nano::work_pool::~work_pool ()
{
	stop ();
	for (auto & i : threads)
	{
		i.join ();
	}
}

void nano::work_pool::loop (uint64_t thread)
{
	// Quick RNG for work attempts.
	xorshift1024star rng;
	nano::random_pool::generate_block (reinterpret_cast<uint8_t *> (rng.s.data ()), rng.s.size () * sizeof (decltype (rng.s)::value_type));
	uint64_t work;
	uint64_t output;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (output));
	std::unique_lock<std::mutex> lock (mutex);
	while (!done || !pending.empty ())
	{
		auto empty (pending.empty ());
		if (thread == 0)
		{
			// Only work thread 0 notifies work observers
			work_observers.notify (!empty);
		}
		if (!empty)
		{
			auto current_l (pending.front ());
			int ticket_l (ticket);
			lock.unlock ();
			output = 0;
			// ticket != ticket_l indicates a different thread found a solution and we should stop
			while (ticket == ticket_l && output < current_l.difficulty)
			{
				// Don't query main memory every iteration in order to reduce memory bus traffic
				// All operations here operate on stack memory
				// Count iterations down to zero since comparing to zero is easier than comparing to another number
				unsigned iteration (256);
				while (iteration && output < current_l.difficulty)
				{
					work = rng.next ();
					blake2b_update (&hash, reinterpret_cast<uint8_t *> (&work), sizeof (work));
					blake2b_update (&hash, current_l.item.bytes.data (), current_l.item.bytes.size ());
					blake2b_final (&hash, reinterpret_cast<uint8_t *> (&output), sizeof (output));
					blake2b_init (&hash, sizeof (output));
					iteration -= 1;
				}
			}
			lock.lock ();
			if (ticket == ticket_l)
			{
				// If the ticket matches what we started with, we're the ones that found the solution
				assert (output >= nano::work_pool::publish_threshold);
				assert (work_value (current_l.item, work) == output);
				// Signal other threads to stop their work next time they check ticket
				++ticket;
				pending.pop_front ();
				lock.unlock ();
				current_l.callback (work);
				lock.lock ();
			}
			else
			{
				// A different thread found a solution
			}
		}
		else
		{
			// Wait for a work request
			producer_condition.wait (lock);
		}
	}
}

void nano::work_pool::cancel (nano::uint256_union const & root_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	if (!pending.empty ())
	{
		if (pending.front ().item == root_a)
		{
			++ticket;
		}
	}
	pending.remove_if ([&root_a](decltype (pending)::value_type const & item_a) {
		bool result;
		if (item_a.item == root_a)
		{
			item_a.callback (boost::none);
			result = true;
		}
		else
		{
			result = false;
		}
		return result;
	});
}

void nano::work_pool::stop ()
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		done = true;
	}
	producer_condition.notify_all ();
}

void nano::work_pool::generate (nano::uint256_union const & root_a, std::function<void(boost::optional<uint64_t> const &)> callback_a, uint64_t difficulty_a)
{
	assert (!root_a.is_zero ());
	boost::optional<uint64_t> result;
	if (opencl)
	{
		result = opencl (root_a);
	}
	if (!result)
	{
		{
			std::lock_guard<std::mutex> lock (mutex);
			pending.push_back ({ root_a, callback_a, difficulty_a });
		}
		producer_condition.notify_all ();
	}
	else
	{
		callback_a (result);
	}
}

uint64_t nano::work_pool::generate (nano::uint256_union const & hash_a, uint64_t difficulty_a)
{
	std::promise<boost::optional<uint64_t>> work;
	std::future<boost::optional<uint64_t>> future = work.get_future ();
	// clang-format off
	generate (hash_a, [&work](boost::optional<uint64_t> work_a) {
		work.set_value (work_a);
	},
	difficulty_a);
	// clang-format on
	auto result (future.get ());
	return result.value ();
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (work_pool & work_pool, const std::string & name)
{
	auto composite = std::make_unique<seq_con_info_composite> (name);

	size_t count = 0;
	{
		std::lock_guard<std::mutex> (work_pool.mutex);
		count = work_pool.pending.size ();
	}
	auto sizeof_element = sizeof (decltype (work_pool.pending)::value_type);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "pending", count, sizeof_element }));
	composite->add_component (collect_seq_con_info (work_pool.work_observers, "work_observers"));
	return composite;
}
}
