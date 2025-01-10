#include <celerix/lib/blocks.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/work.hpp>
#include <celerix/node/make_store.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/secure/utility.hpp>
#include <celerix/store/component.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (processor_service, bad_send_signature)
{
	celerix::test::system system;

	auto store = celerix::make_store (system.logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_FALSE (store->init_error ());
	celerix::ledger ledger (*store, system.stats, celerix::dev::constants);
	auto transaction = ledger.tx_begin_write ();
	store->initialize (transaction, ledger.cache, ledger.constants);
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	auto info1 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	celerix::keypair key2;
	celerix::block_builder builder;
	auto send = builder
				.send ()
				.previous (info1->head)
				.destination (celerix::dev::genesis_key.pub)
				.balance (50)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*pool.generate (info1->head))
				.build ();
	send->signature.bytes[32] ^= 0x1;
	ASSERT_EQ (celerix::block_status::bad_signature, ledger.process (transaction, send));
}

TEST (processor_service, bad_receive_signature)
{
	celerix::test::system system;

	auto store = celerix::make_store (system.logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_FALSE (store->init_error ());
	celerix::ledger ledger (*store, system.stats, celerix::dev::constants);
	auto transaction = ledger.tx_begin_write ();
	store->initialize (transaction, ledger.cache, ledger.constants);
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	auto info1 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	celerix::block_builder builder;
	auto send = builder
				.send ()
				.previous (info1->head)
				.destination (celerix::dev::genesis_key.pub)
				.balance (50)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*pool.generate (info1->head))
				.build ();
	celerix::block_hash hash1 (send->hash ());
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
	auto info2 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info2);
	auto receive = builder
				   .receive ()
				   .previous (hash1)
				   .source (hash1)
				   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				   .work (*pool.generate (hash1))
				   .build ();
	receive->signature.bytes[32] ^= 0x1;
	ASSERT_EQ (celerix::block_status::bad_signature, ledger.process (transaction, receive));
}
