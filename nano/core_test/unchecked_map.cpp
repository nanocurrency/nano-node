#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/node/unchecked_map.hpp>
#include <nano/secure/store.hpp>
#include <nano/secure/utility.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <memory>

using namespace std::chrono_literals;

namespace
{
class context
{
public:
	context () :
		store{ nano::make_store (logger, nano::unique_path (), nano::dev::constants) },
		unchecked{ *store, false }
	{
	}
	nano::logger_mt logger;
	std::unique_ptr<nano::store> store;
	nano::unchecked_map unchecked;
};
std::shared_ptr<nano::block> block ()
{
	nano::block_builder builder;
	return builder.state ()
	.account (nano::dev::genesis_key.pub)
	.previous (nano::dev::genesis->hash ())
	.representative (nano::dev::genesis_key.pub)
	.balance (nano::dev::constants.genesis_amount - 1)
	.link (nano::dev::genesis_key.pub)
	.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
	.work (0)
	.build_shared ();
}
}

TEST (unchecked_map, construction)
{
	context context;
}

TEST (unchecked_map, put_one)
{
	context context;
	nano::unchecked_info info{ block (), nano::dev::genesis_key.pub };
	context.unchecked.put (info.block->previous (), info);
}

TEST (block_store, one_bootstrap)
{
	nano::system system{};
	nano::logger_mt logger{};
	auto store = nano::make_store (logger, nano::unique_path (), nano::dev::constants);
	nano::unchecked_map unchecked{ *store, false };
	ASSERT_TRUE (!store->init_error ());
	auto block1 = std::make_shared<nano::send_block> (0, 1, 2, nano::keypair ().prv, 4, 5);
	unchecked.put (block1->hash (), nano::unchecked_info{ block1 });
	auto check_block_is_listed = [&] (nano::transaction const & transaction_a, nano::block_hash const & block_hash_a) {
		return unchecked.get (transaction_a, block_hash_a).size () > 0;
	};
	// Waits for the block1 to get saved in the database
	ASSERT_TIMELY (10s, check_block_is_listed (store->tx_begin_read (), block1->hash ()));
	auto transaction = store->tx_begin_read ();
	std::vector<nano::block_hash> dependencies;
	unchecked.for_each (transaction, [&dependencies] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
		dependencies.push_back (key.key ());
	});
	auto hash1 = dependencies[0];
	ASSERT_EQ (block1->hash (), hash1);
	auto blocks = unchecked.get (transaction, hash1);
	ASSERT_EQ (1, blocks.size ());
	auto block2 = blocks[0].block;
	ASSERT_EQ (*block1, *block2);
}
