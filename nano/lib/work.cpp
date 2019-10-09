#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/lambda_visitor.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/xorshift.hpp>

#include <boost/endian/conversion.hpp>

#include <future>

bool nano::work_validate (nano::root const & root_a, nano::proof_of_work work_a, uint64_t * difficulty_a)
{
	static nano::network_constants network_constants;
	auto value (nano::work_value (root_a, work_a));
	if (difficulty_a != nullptr)
	{
		*difficulty_a = value;
	}
	return value < network_constants.publish_threshold;
}

bool nano::work_validate (nano::block const & block_a, uint64_t * difficulty_a)
{
	return work_validate (block_a.root (), block_a.block_work (), difficulty_a);
}

uint64_t nano::work_value (nano::root const & root_a, nano::proof_of_work work_a)
{
	uint64_t result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result));
	boost::apply_visitor (nano::make_lambda_visitor<void> (
	                      [&hash](nano::legacy_pow const & legacy_pow) {
		                      blake2b_update (&hash, reinterpret_cast<const uint8_t *> (&legacy_pow), sizeof (legacy_pow));
	                      },
	                      [&hash](nano::nano_pow const & nano_pow) {
		                      auto little_endian = nano_pow.bytes;
		                      std::reverse (little_endian.begin (), little_endian.end ());
		                      // Temp: Only take the first few bytes for now to match the legacy pow
		                      std::array<uint8_t, sizeof (nano::legacy_pow)> legacy_pow_subset;
		                      std::copy (little_endian.begin (), little_endian.begin () + sizeof (nano::legacy_pow), legacy_pow_subset.begin ());
		                      blake2b_update (&hash, reinterpret_cast<const uint8_t *> (&legacy_pow_subset), sizeof (legacy_pow_subset));
	                      }),
	work_a.pow);
	blake2b_update (&hash, root_a.bytes.data (), root_a.bytes.size ());
	blake2b_final (&hash, reinterpret_cast<uint8_t *> (&result), sizeof (result));
	return result;
}

nano::work_pool::work_pool (unsigned max_threads_a, std::chrono::nanoseconds pow_rate_limiter_a, std::function<boost::optional<nano::proof_of_work> (nano::root const &, uint64_t, std::atomic<int> &)> opencl_a) :
ticket (0),
done (false),
pow_rate_limiter (pow_rate_limiter_a),
opencl (opencl_a)
{
	static_assert (ATOMIC_INT_LOCK_FREE == 2, "Atomic int needed");
	boost::thread::attributes attrs;
	nano::thread_attributes::set (attrs);
	auto count (network_constants.is_test_network () ? std::min (max_threads_a, 1u) : std::min (max_threads_a, std::max (1u, boost::thread::hardware_concurrency ())));
	if (opencl)
	{
		// One thread to handle OpenCL
		++count;
	}
	for (auto i (0u); i < count; ++i)
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
	nano::legacy_pow work;
	nano::legacy_pow output;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (output));
	nano::unique_lock<std::mutex> lock (mutex);
	auto pow_sleep = pow_rate_limiter;
	while (!done)
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
			boost::optional<nano::proof_of_work> opt_work;
			if (thread == 0 && opencl)
			{
				opt_work = opencl (current_l.item, current_l.difficulty, ticket);
			}
			if (opt_work.is_initialized ())
			{
				work = *opt_work;
				output = work_value (current_l.item, work);
			}
			else
			{
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

					// Add a rate limiter (if specified) to the pow calculation to save some CPUs which don't want to operate at full throttle
					if (pow_sleep != std::chrono::nanoseconds (0))
					{
						std::this_thread::sleep_for (pow_sleep);
					}
				}
			}
			lock.lock ();
			if (ticket == ticket_l)
			{
				// If the ticket matches what we started with, we're the ones that found the solution
				assert (output >= current_l.difficulty);
				assert (current_l.difficulty == 0 || work_value (current_l.item, work) == output);

				nano::proof_of_work pow;
				if (nano::is_epoch_nano_pow (current_l.epoch))
				{
					// Temp: wrap in a nano_pow if the version of work requires it
					pow = nano::nano_pow (work);
				}
				else
				{
					pow = work;
				}

				// Signal other threads to stop their work next time they check ticket
				++ticket;
				pending.pop_front ();
				lock.unlock ();
				current_l.callback (pow);
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

void nano::work_pool::cancel (nano::root const & root_a)
{
	nano::lock_guard<std::mutex> lock (mutex);
	if (!done)
	{
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
				if (item_a.callback)
				{
					item_a.callback (boost::none);
				}
				result = true;
			}
			else
			{
				result = false;
			}
			return result;
		});
	}
}

void nano::work_pool::stop ()
{
	{
		nano::lock_guard<std::mutex> lock (mutex);
		done = true;
		++ticket;
	}
	producer_condition.notify_all ();
}

void nano::work_pool::generate (nano::root const & root_a, std::function<void(boost::optional<nano::proof_of_work> const &)> callback_a, nano::epoch epoch_a)
{
	generate (root_a, callback_a, network_constants.publish_threshold, epoch_a);
}

void nano::work_pool::generate (nano::root const & root_a, std::function<void(boost::optional<nano::proof_of_work> const &)> callback_a, uint64_t difficulty_a, nano::epoch epoch_a)
{
	assert (!root_a.is_zero ());
	if (!threads.empty ())
	{
		{
			nano::lock_guard<std::mutex> lock (mutex);
			pending.emplace_back (root_a, callback_a, difficulty_a, epoch_a);
		}
		producer_condition.notify_all ();
	}
	else if (callback_a)
	{
		callback_a (boost::none);
	}
}

boost::optional<nano::proof_of_work> nano::work_pool::generate (nano::root const & root_a, nano::epoch epoch_a)
{
	return generate (root_a, network_constants.publish_threshold, epoch_a);
}

boost::optional<nano::proof_of_work> nano::work_pool::generate (nano::root const & root_a, uint64_t difficulty_a, nano::epoch epoch_a)
{
	boost::optional<nano::proof_of_work> result;
	if (!threads.empty ())
	{
		std::promise<boost::optional<nano::proof_of_work>> work;
		std::future<boost::optional<nano::proof_of_work>> future = work.get_future ();
		// clang-format off
		generate (root_a, [&work](boost::optional<nano::proof_of_work> work_a) {
			work.set_value (work_a);
		},
		difficulty_a, epoch_a);
		// clang-format on
		result = future.get ().value ();
	}
	return result;
}

size_t nano::work_pool::size ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	return pending.size ();
}

std::unique_ptr<nano::seq_con_info_component> nano::collect_seq_con_info (work_pool & work_pool, const std::string & name)
{
	auto composite = std::make_unique<seq_con_info_composite> (name);

	size_t count = 0;
	{
		nano::lock_guard<std::mutex> guard (work_pool.mutex);
		count = work_pool.pending.size ();
	}
	auto sizeof_element = sizeof (decltype (work_pool.pending)::value_type);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "pending", count, sizeof_element }));
	composite->add_component (collect_seq_con_info (work_pool.work_observers, "work_observers"));
	return composite;
}

nano::proof_of_work::proof_of_work (nano::legacy_pow pow_a) :
pow (pow_a)
{
}

nano::proof_of_work::proof_of_work (nano::nano_pow pow_a) :
pow (pow_a)
{
}

nano::proof_of_work::operator nano::legacy_pow () const
{
	// Currently a hack if trying to use this in a context where the legacy_pow is used
	assert (is_legacy ());
	return boost::get<legacy_pow> (pow);
}

nano::proof_of_work::operator nano::nano_pow const & () const
{
	assert (!is_legacy ());
	return boost::get<nano_pow> (pow);
}

void nano::proof_of_work::deserialize (nano::stream & stream_a, bool is_legacy_a)
{
	if (is_legacy_a)
	{
		nano::legacy_pow work;
		nano::read (stream_a, work);
		boost::endian::big_to_native_inplace (work);
		pow = work;
	}
	else
	{
		nano::nano_pow work;
		nano::read (stream_a, work);
		pow = work;
	}
}

void nano::proof_of_work::serialize (nano::stream & stream_a) const
{
	return boost::apply_visitor (make_lambda_visitor<void> (
	                             [&stream_a](nano::legacy_pow const & pow) {
		                             write (stream_a, boost::endian::native_to_big (pow));
	                             },
	                             [&stream_a](nano::nano_pow const & pow) {
		                             write (stream_a, pow);
	                             }),
	pow);
}

bool nano::proof_of_work::is_empty () const
{
	return boost::apply_visitor (make_lambda_visitor<bool> (
	                             [](nano::legacy_pow const & pow) {
		                             return pow == 0;
	                             },
	                             [](nano::nano_pow const & pow) {
		                             return std::all_of (pow.bytes.begin (), pow.bytes.end (), [](auto i) { return i == 0; });
	                             }),
	pow);
}

bool nano::proof_of_work::operator== (nano::proof_of_work const & other_a) const
{
	return pow == other_a.pow;
}

bool nano::proof_of_work::is_legacy () const
{
	return pow.type () == typeid (nano::legacy_pow);
}

size_t nano::proof_of_work::get_sizeof () const
{
	return boost::apply_visitor (make_lambda_visitor<size_t> ([](auto const & pow) {
		return sizeof (pow);
	}),
	pow);
}

nano::proof_of_work & nano::proof_of_work::operator++ ()
{
	boost::apply_visitor (make_lambda_visitor<void> ([](auto & pow) {
		++pow;
	}),
	pow);
	return *this;
}

std::string nano::to_string_hex (nano::proof_of_work const & value_a)
{
	return boost::apply_visitor (make_lambda_visitor<std::string> ([](auto & pow) {
		return to_string_hex (pow);
	}),
	value_a.pow);
}

bool nano::from_string_hex (std::string const & value_a, nano::proof_of_work & target_a, nano::epoch epoch_a)
{
	return from_string_hex (value_a, target_a, !nano::is_epoch_nano_pow (epoch_a));
}

bool nano::from_string_hex (std::string const & value_a, nano::proof_of_work & target_a, bool is_legacy_work_a)
{
	bool error;
	if (is_legacy_work_a)
	{
		nano::legacy_pow pow;
		error = nano::from_string_hex (value_a, pow);
		target_a = pow;
	}
	else
	{
		nano::nano_pow pow;
		error = nano::from_string_hex (value_a, pow);
		target_a = pow;
	}
	return error;
}
