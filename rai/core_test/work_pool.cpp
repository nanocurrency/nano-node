#include <gtest/gtest.h>

#include <rai/node/node.hpp>
#include <rai/node/wallet.hpp>
#include <rai/node/openclwork.hpp>

TEST (work, one)
{
	rai::work_pool pool (nullptr);
    rai::change_block block (1, 1, rai::keypair ().prv, 3, 4);
    block.block_work_set (pool.generate (block.root ()));
    ASSERT_FALSE (pool.work_validate (block));
}

TEST (work, validate)
{
	rai::work_pool pool (nullptr);
	rai::send_block send_block (1, 1, 2, rai::keypair ().prv, 4, 6);
    ASSERT_TRUE (pool.work_validate (send_block));
    send_block.block_work_set (pool.generate (send_block.root ()));
    ASSERT_FALSE (pool.work_validate (send_block));
}

TEST (work, cancel)
{
	rai::work_pool pool (nullptr);
	rai::uint256_union key (1);
	bool exited (false);
	std::thread thread ([&pool, &key, &exited] ()
	{
		auto maybe (pool.generate_maybe (key));
		exited = true;
	});
	auto done (false);
	while (!done)
	{
		std::lock_guard <std::mutex> lock (pool.mutex);
		done = exited || !pool.pending.empty ();
	}
	pool.cancel (key);
	thread.join ();
}

TEST (work, cancel_many)
{
	rai::work_pool pool (nullptr);
	rai::uint256_union key1 (1);
	rai::uint256_union key2 (2);
	rai::uint256_union key3 (1);
	rai::uint256_union key4 (1);
	rai::uint256_union key5 (3);
	rai::uint256_union key6 (1);
	std::thread thread1 ([&key1, &pool]()
	{
		pool.generate_maybe (key1);
	});
	std::thread thread2 ([&key2, &pool]()
	{
		pool.generate_maybe (key2);
	});
	std::thread thread3 ([&key3, &pool]()
	{
		pool.generate_maybe (key3);
	});
	std::thread thread4 ([&key4, &pool]()
	{
		pool.generate_maybe (key4);
	});
	std::thread thread5 ([&key5, &pool]()
	{
		pool.generate_maybe (key5);
	});
	std::thread thread6 ([&key6, &pool]()
	{
		pool.generate_maybe (key6);
	});
	pool.cancel (key1);
	thread1.join ();
	thread2.join ();
	thread3.join ();
	thread4.join ();
	thread5.join ();
	thread6.join ();
}

TEST (work, opencl)
{
	rai::logging logging (rai::unique_path ());
	auto work (rai::opencl_work::create (true, {0, 1, 1024 * 1024}, logging));
	if (work != nullptr)
	{
		rai::work_pool pool (std::move (work));
		ASSERT_NE (nullptr, pool.opencl);
		rai::uint256_union root;
		for (auto i (0); i < 1; ++i)
		{
			rai::random_pool.GenerateBlock (root.bytes.data (), root.bytes.size ());
			auto result (pool.generate (root));
			ASSERT_FALSE (pool.work_validate (root, result));
		}
	}
}

TEST (work, opencl_config)
{
	rai::opencl_config config1;
	config1.platform = 1;
	config1.device = 2;
	config1.threads = 3;
	boost::property_tree::ptree tree;
	config1.serialize_json (tree);
	rai::opencl_config config2;
	ASSERT_FALSE (config2.deserialize_json (tree));
	ASSERT_EQ (1, config2.platform);
	ASSERT_EQ (2, config2.device);
	ASSERT_EQ (3, config2.threads);
}
