#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (ssl_node, basic_test_no_ssl)
{
	nano::test::system system{};

	nano::node_flags flags_1{};
	flags_1.disable_ssl_sockets = true;

	auto node_1 = system.add_node (flags_1);
	std::cout << "node port: " << node_1->network.endpoint ().port () << std::endl;
	ASSERT_EQ (0, node_1->network.size ());

	nano::node_flags flags_2{};
	flags_2.disable_ssl_sockets = true;

	auto node_2 = system.add_node (flags_2);
	std::cout << "node port: " << node_2->network.endpoint ().port () << std::endl;
	ASSERT_EQ (1, node_1->network.size ());
	ASSERT_EQ (1, node_2->network.size ());
}

TEST (ssl_node, basic_test_two_nodes)
{
	nano::test::system system{};

	nano::node_flags flags_1{};
	flags_1.disable_ssl_sockets = false;

	auto node_1 = system.add_node (nano::node_config{ 37000, system.logging }, flags_1);
	std::cout << "node port: " << node_1->network.endpoint ().port () << std::endl;
	ASSERT_EQ (0, node_1->network.size ());

	nano::node_flags flags_2{};
	flags_2.disable_ssl_sockets = false;

	auto node_2 = system.add_node (nano::node_config{ 37001, system.logging }, flags_2);
	std::cout << "node port: " << node_2->network.endpoint ().port () << std::endl;
	ASSERT_EQ (1, node_1->network.size ());
	ASSERT_EQ (1, node_2->network.size ());
}

TEST (ssl_node, basic_test_one_node)
{
	nano::test::system system{};

	nano::node_flags flags_1{};
	flags_1.disable_ssl_sockets = false;

	auto node_1 = system.add_node (flags_1);
	std::cout << "node port: " << node_1->network.endpoint ().port () << std::endl;
	ASSERT_EQ (0, node_1->network.size ());
}
