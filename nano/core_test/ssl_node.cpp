#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (ssl_node, basic_test_no_ssl)
{
	nano::test::system system{};

	nano::node_config config_1{};
	config_1.network_params.network.ssl_support_enabled = false;

	auto node_1 = system.add_node (config_1);
	std::cout << "node port: " << node_1->network.endpoint ().port () << std::endl;
	ASSERT_EQ (0, node_1->network.size ());

	nano::node_config config_2{};
	config_2.network_params.network.ssl_support_enabled = false;

	auto node_2 = system.add_node (config_2);
	std::cout << "node port: " << node_2->network.endpoint ().port () << std::endl;
	ASSERT_EQ (1, node_1->network.size ());
	ASSERT_EQ (1, node_2->network.size ());
}

TEST (ssl_node, basic_test_two_nodes)
{
	nano::test::system system{};

	nano::node_config config_1{ 37000, system.logging };
	config_1.network_params.network.ssl_support_enabled = true;

	auto node_1 = system.add_node (config_1);
	std::cout << "node port: " << node_1->network.endpoint ().port () << std::endl;
	ASSERT_EQ (0, node_1->network.size ());

	nano::node_config config_2{ 37001, system.logging };
	config_2.network_params.network.ssl_support_enabled = true;

	auto node_2 = system.add_node (config_2);
	std::cout << "node port: " << node_2->network.endpoint ().port () << std::endl;
	ASSERT_EQ (1, node_1->network.size ());
	ASSERT_EQ (1, node_2->network.size ());
}

TEST (ssl_node, basic_test_one_node)
{
	nano::test::system system{};

	nano::node_config config_1{};
	config_1.network_params.network.ssl_support_enabled = true;

	auto node_1 = system.add_node (config_1);
	std::cout << "node port: " << node_1->network.endpoint ().port () << std::endl;
	ASSERT_EQ (0, node_1->network.size ());
}
