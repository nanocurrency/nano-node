#include <gtest/gtest.h>
#include <rai/node/wallet.hpp>

TEST (work, one)
{
	rai::work_pool pool;
    rai::change_block block (1, 1, rai::keypair ().prv, 3, 4);
    block.block_work_set (pool.generate (block.root ()));
    ASSERT_FALSE (pool.work_validate (block));
}

TEST (work, validate)
{
	rai::work_pool pool;
	rai::send_block send_block (1, 1, 2, rai::keypair ().prv, 4, 6);
    ASSERT_TRUE (pool.work_validate (send_block));
    send_block.block_work_set (pool.generate (send_block.root ()));
    ASSERT_FALSE (pool.work_validate (send_block));
}

TEST (work, cancel)
{
	rai::work_pool pool;
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
		done = exited || !pool.pending.empty () || !pool.current.is_zero ();
	}
	pool.cancel (key);
	thread.join ();
}