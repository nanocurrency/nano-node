#include <celerix/crypto_lib/random_pool.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/lib/work.hpp>
#include <celerix/lib/work_version.hpp>
#include <celerix/node/openclconfig.hpp>
#include <celerix/node/openclwork.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/utility.hpp>

#include <gtest/gtest.h>

#include <future>

// produce one proof of work for a block and check that its difficulty is higher than the base difficulty
TEST (work, one)
{
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::block_builder builder;
	auto block = builder
				 .change ()
				 .previous (1)
				 .representative (1)
				 .sign (celerix::keypair ().prv, 3)
				 .work (4)
				 .build ();
	block->block_work_set (*pool.generate (block->root ()));
	ASSERT_LT (celerix::dev::network_params.work.threshold_base (block->work_version ()), celerix::dev::network_params.work.difficulty (*block));
}

// create a work_pool with zero threads and check that pool.generate returns no result
TEST (work, disabled)
{
	celerix::work_pool pool{ celerix::dev::network_params.network, 0 };
	auto result (pool.generate (celerix::block_hash ()));
	ASSERT_FALSE (result.is_initialized ());
}

// create a block with bad pow then fix it and check that it validates
TEST (work, validate)
{
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::block_builder builder;
	auto send_block = builder
					  .send ()
					  .previous (1)
					  .destination (1)
					  .balance (2)
					  .sign (celerix::keypair ().prv, 4)
					  .work (6)
					  .build ();
	ASSERT_LT (celerix::dev::network_params.work.difficulty (*send_block), celerix::dev::network_params.work.threshold_base (send_block->work_version ()));
	send_block->block_work_set (*pool.generate (send_block->root ()));
	ASSERT_GE (celerix::dev::network_params.work.difficulty (*send_block), celerix::dev::network_params.work.threshold_base (send_block->work_version ()));
}

// repeatedly start and cancel a work calculation and check that the callback is eventually called
TEST (work, cancel)
{
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	const celerix::root key (1);
	auto iterations = 0;
	auto done = false;
	while (!done)
	{
		pool.generate (celerix::work_version::work_1, key, celerix::dev::network_params.work.base, [&done] (boost::optional<uint64_t> work_a) {
			done = !work_a;
		});
		pool.cancel (key);
		++iterations;
		ASSERT_LT (iterations, 200);
	}
}

TEST (work, cancel_one_out_of_many)
{
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::root key1 (1);
	celerix::root key2 (2);
	celerix::root key3 (1);
	celerix::root key4 (1);
	celerix::root key5 (3);
	celerix::root key6 (1);
	pool.generate (celerix::work_version::work_1, key1, celerix::dev::network_params.work.base, [] (boost::optional<uint64_t>) {});
	pool.generate (celerix::work_version::work_1, key2, celerix::dev::network_params.work.base, [] (boost::optional<uint64_t>) {});
	pool.generate (celerix::work_version::work_1, key3, celerix::dev::network_params.work.base, [] (boost::optional<uint64_t>) {});
	pool.generate (celerix::work_version::work_1, key4, celerix::dev::network_params.work.base, [] (boost::optional<uint64_t>) {});
	pool.generate (celerix::work_version::work_1, key5, celerix::dev::network_params.work.base, [] (boost::optional<uint64_t>) {});
	pool.generate (celerix::work_version::work_1, key6, celerix::dev::network_params.work.base, [] (boost::optional<uint64_t>) {});
	pool.cancel (key1);
}

// check that opencl hardware offloading works
TEST (work, opencl)
{
	celerix::logger logger;
	bool error = false;
	celerix::opencl_environment environment (error);
	ASSERT_TRUE (!error || !celerix::opencl_loaded);

	if (environment.platforms.empty () || environment.platforms.begin ()->devices.empty ())
	{
		GTEST_SKIP () << "Device with OpenCL support not found. Skipping OpenCL test" << std::endl;
	}

	celerix::opencl_config config (0, 0, 16 * 1024);
	auto opencl = celerix::opencl_work::create (true, config, logger, celerix::dev::network_params.work);
	ASSERT_TRUE (opencl);

	// 0 threads, should add 1 for managing OpenCL
	bool opencl_function_called = false;
	celerix::work_pool pool{ celerix::dev::network_params.network, 0, std::chrono::celerixseconds (0),
		[&opencl, &opencl_function_called] (celerix::work_version const version_a, celerix::root const & root_a, uint64_t difficulty_a, std::atomic<int> & ticket_a) {
			opencl_function_called = true;
			return opencl->generate_work (version_a, root_a, difficulty_a);
		} };
	ASSERT_NE (nullptr, pool.opencl);

	celerix::root root;
	uint64_t difficulty (0xffff000000000000);
	uint64_t difficulty_add (0x00000f0000000000);
	for (auto i (0); i < 16; ++i)
	{
		celerix::random_pool::generate_block (root.bytes.data (), root.bytes.size ());
		auto nonce_opt = pool.generate (celerix::work_version::work_1, root, difficulty);
		ASSERT_TRUE (nonce_opt.has_value ());
		auto nonce = nonce_opt.get ();
		ASSERT_GE (celerix::dev::network_params.work.difficulty (celerix::work_version::work_1, root, nonce), difficulty);
		difficulty += difficulty_add;
	}
	ASSERT_TRUE (opencl_function_called);
}

// repeat difficulty calculations until a difficulty in a certain range is found
TEST (work, difficulty)
{
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	const celerix::root root (1);
	uint64_t difficulty1 = 0xff00000000000000;
	uint64_t difficulty2 = 0xfff0000000000000;
	uint64_t difficulty3 = 0xffff000000000000;

	// find a difficulty between difficulty1 and difficulty2
	uint64_t result_difficulty1 = 0;
	do
	{
		auto work1 = *pool.generate (celerix::work_version::work_1, root, difficulty1);
		result_difficulty1 = celerix::dev::network_params.work.difficulty (celerix::work_version::work_1, root, work1);
	} while (result_difficulty1 > difficulty2);
	ASSERT_GT (result_difficulty1, difficulty1);

	// find a difficulty between difficulty2 and difficulty3
	uint64_t result_difficulty2 (0);
	do
	{
		auto work2 = *pool.generate (celerix::work_version::work_1, root, difficulty2);
		result_difficulty2 = celerix::dev::network_params.work.difficulty (celerix::work_version::work_1, root, work2);
	} while (result_difficulty2 > difficulty3);
	ASSERT_GT (result_difficulty2, difficulty2);
}

// check that the pow_rate_limiter of work_pool works, this test can fail occasionally
TEST (work, eco_pow)
{
	auto work_func = [] (std::promise<std::chrono::celerixseconds> & promise, std::chrono::celerixseconds interval) {
		celerix::work_pool pool{ celerix::dev::network_params.network, 1, interval };
		constexpr auto num_iterations = 5;

		celerix::timer<std::chrono::celerixseconds> timer;
		timer.start ();
		for (int i = 0; i < num_iterations; ++i)
		{
			celerix::root root (1);
			uint64_t difficulty1 (0xff00000000000000);
			uint64_t difficulty2 (0xfff0000000000000);
			uint64_t result_difficulty (0);
			do
			{
				auto work = *pool.generate (celerix::work_version::work_1, root, difficulty1);
				result_difficulty = celerix::dev::network_params.work.difficulty (celerix::work_version::work_1, root, work);
			} while (result_difficulty > difficulty2);
			ASSERT_GT (result_difficulty, difficulty1);
		}

		promise.set_value_at_thread_exit (timer.stop ());
	};

	std::promise<std::chrono::celerixseconds> promise1;
	std::future<std::chrono::celerixseconds> future1 = promise1.get_future ();
	std::promise<std::chrono::celerixseconds> promise2;
	std::future<std::chrono::celerixseconds> future2 = promise2.get_future ();

	std::thread thread1 (work_func, std::ref (promise1), std::chrono::celerixseconds (0));
	std::thread thread2 (work_func, std::ref (promise2), std::chrono::milliseconds (10));

	thread1.join ();
	thread2.join ();

	// Confirm that the eco pow rate limiter is working.
	// It's possible under some unlucky circumstances that this fails to the random nature of valid work generation.
	ASSERT_LT (future1.get (), future2.get ());
}
