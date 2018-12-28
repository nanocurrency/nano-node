#include <gtest/gtest.h>

#include <rai/node/node.hpp>
#include <rai/node/wallet.hpp>

TEST (work, one)
{
	rai::work_pool pool (std::numeric_limits<unsigned>::max (), nullptr);
	rai::change_block block (1, 1, rai::keypair ().prv, 3, 4);
	block.block_work_set (pool.generate (block.root ()));
	uint64_t difficulty;
	ASSERT_FALSE (rai::work_validate (block, &difficulty));
	ASSERT_LT (rai::work_pool::publish_threshold, difficulty);
}

TEST (work, validate)
{
	rai::work_pool pool (std::numeric_limits<unsigned>::max (), nullptr);
	rai::send_block send_block (1, 1, 2, rai::keypair ().prv, 4, 6);
	uint64_t difficulty;
	ASSERT_TRUE (rai::work_validate (send_block, &difficulty));
	ASSERT_LT (difficulty, rai::work_pool::publish_threshold);
	send_block.block_work_set (pool.generate (send_block.root ()));
	ASSERT_FALSE (rai::work_validate (send_block, &difficulty));
	ASSERT_LT (rai::work_pool::publish_threshold, difficulty);
}

TEST (work, cancel)
{
	rai::work_pool pool (std::numeric_limits<unsigned>::max (), nullptr);
	auto iterations (0);
	auto done (false);
	while (!done)
	{
		rai::uint256_union key (1);
		pool.generate (key, [&done](boost::optional<uint64_t> work_a) {
			done = !work_a;
		});
		pool.cancel (key);
		++iterations;
		ASSERT_LT (iterations, 200);
	}
}

TEST (work, cancel_many)
{
	rai::work_pool pool (std::numeric_limits<unsigned>::max (), nullptr);
	rai::uint256_union key1 (1);
	rai::uint256_union key2 (2);
	rai::uint256_union key3 (1);
	rai::uint256_union key4 (1);
	rai::uint256_union key5 (3);
	rai::uint256_union key6 (1);
	pool.generate (key1, [](boost::optional<uint64_t>) {});
	pool.generate (key2, [](boost::optional<uint64_t>) {});
	pool.generate (key3, [](boost::optional<uint64_t>) {});
	pool.generate (key4, [](boost::optional<uint64_t>) {});
	pool.generate (key5, [](boost::optional<uint64_t>) {});
	pool.generate (key6, [](boost::optional<uint64_t>) {});
	pool.cancel (key1);
}

TEST (work, DISABLED_opencl)
{
	rai::logging logging;
	logging.init (rai::unique_path ());
	auto opencl (rai::opencl_work::create (true, { 0, 1, 1024 * 1024 }, logging));
	if (opencl != nullptr)
	{
		rai::work_pool pool (std::numeric_limits<unsigned>::max (), opencl ? [&opencl](rai::uint256_union const & root_a) {
			return opencl->generate_work (root_a);
		}
		                                                                   : std::function<boost::optional<uint64_t> (rai::uint256_union const &)> (nullptr));
		ASSERT_NE (nullptr, pool.opencl);
		rai::uint256_union root;
		for (auto i (0); i < 1; ++i)
		{
			rai::random_pool.GenerateBlock (root.bytes.data (), root.bytes.size ());
			auto result (pool.generate (root));
			ASSERT_FALSE (rai::work_validate (root, result));
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

TEST (work, difficulty)
{
	rai::work_pool pool (std::numeric_limits<unsigned>::max (), nullptr);
	rai::uint256_union root (1);
	uint64_t difficulty1 (0xff00000000000000);
	uint64_t difficulty2 (0xfff0000000000000);
	uint64_t difficulty3 (0xffff000000000000);
	uint64_t work1 (0);
	uint64_t nonce1 (0);
	do
	{
		work1 = pool.generate (root, difficulty1);
		rai::work_validate (root, work1, &nonce1);
	} while (nonce1 > difficulty2);
	ASSERT_GT (nonce1, difficulty1);
	uint64_t work2 (0);
	uint64_t nonce2 (0);
	do
	{
		work2 = pool.generate (root, difficulty2);
		rai::work_validate (root, work2, &nonce2);
	} while (nonce2 > difficulty3);
	ASSERT_GT (nonce2, difficulty2);
}
