#include <nano/lib/container_info.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/node/peer_blocklist.hpp>

#include <gtest/gtest.h>

/*
 * Node id entries are matched exactly and can be added and removed once each
 */
TEST (peer_blocklist, node_id)
{
	nano::peer_blocklist blocklist;
	nano::keypair key1;
	nano::keypair key2;
	ASSERT_TRUE (blocklist.empty ());
	ASSERT_FALSE (blocklist.contains (key1.pub));

	ASSERT_TRUE (blocklist.add (key1.pub));
	ASSERT_FALSE (blocklist.add (key1.pub)); // Already present
	ASSERT_TRUE (blocklist.contains (key1.pub));
	ASSERT_FALSE (blocklist.contains (key2.pub));
	ASSERT_EQ (1, blocklist.size ());

	ASSERT_TRUE (blocklist.remove (key1.pub));
	ASSERT_FALSE (blocklist.remove (key1.pub)); // Already removed
	ASSERT_FALSE (blocklist.contains (key1.pub));
	ASSERT_TRUE (blocklist.empty ());
}

/*
 * An IPv4 address is one entry whether it is spelled plain or in the v4 mapped IPv6 form peers connect with
 */
TEST (peer_blocklist, address_v4_mapped)
{
	nano::peer_blocklist blocklist;
	auto const plain = boost::asio::ip::make_address ("192.0.2.1");
	auto const mapped = boost::asio::ip::make_address ("::ffff:192.0.2.1");
	auto const other = boost::asio::ip::make_address ("192.0.2.2");

	ASSERT_TRUE (blocklist.add (plain));
	ASSERT_FALSE (blocklist.add (mapped)); // Same entry
	ASSERT_EQ (1, blocklist.size ());
	ASSERT_TRUE (blocklist.contains (plain));
	ASSERT_TRUE (blocklist.contains (mapped));
	ASSERT_FALSE (blocklist.contains (other));

	ASSERT_TRUE (blocklist.remove (mapped));
	ASSERT_FALSE (blocklist.contains (plain));
	ASSERT_TRUE (blocklist.empty ());
}

/*
 * IPv6 entries block a single address, not its neighbours in the same subnet
 */
TEST (peer_blocklist, address_v6)
{
	nano::peer_blocklist blocklist;
	auto const address = boost::asio::ip::make_address ("2001:db8::1");
	auto const neighbour = boost::asio::ip::make_address ("2001:db8::2");

	ASSERT_TRUE (blocklist.add (address));
	ASSERT_TRUE (blocklist.contains (address));
	ASSERT_FALSE (blocklist.contains (neighbour));

	ASSERT_TRUE (blocklist.remove (address));
	ASSERT_FALSE (blocklist.remove (neighbour)); // Never present
	ASSERT_TRUE (blocklist.empty ());
}

/*
 * Node ids and addresses are independent entries that are counted together and reported apart
 */
TEST (peer_blocklist, mixed)
{
	nano::peer_blocklist blocklist;
	nano::keypair key;
	auto const address = boost::asio::ip::make_address ("192.0.2.1");

	ASSERT_TRUE (blocklist.add (key.pub));
	ASSERT_TRUE (blocklist.add (address));
	ASSERT_EQ (2, blocklist.size ());

	ASSERT_TRUE (blocklist.remove (key.pub));
	ASSERT_EQ (1, blocklist.size ());
	ASSERT_TRUE (blocklist.contains (address));
	ASSERT_FALSE (blocklist.contains (key.pub));

	auto const info = blocklist.container_info ();
	ASSERT_EQ (2, info.entries ().size ());
	auto entry = info.entries ().begin ();
	ASSERT_EQ ("node_ids", entry->name);
	ASSERT_EQ (0, entry->size);
	++entry;
	ASSERT_EQ ("addresses", entry->name);
	ASSERT_EQ (1, entry->size);
}
