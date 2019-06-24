#include <nano/core_test/testutil.hpp>
#include <nano/node/testing.hpp>

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace
{
std::shared_ptr<nano::system> system1;
std::shared_ptr<nano::node> node0;
std::shared_ptr<nano::node> node1;
std::shared_ptr<nano::transport::channel_udp> node0channel0;
std::shared_ptr<nano::thread_runner> runner;
std::atomic<int> concurrent_calls{ false };
}
namespace nano
{
void force_nano_test_network ();
/** Clean up test directories on process tear-down */
void signal_handler (int signal)
{
	runner->stop_event_processing ();
	std::exit (signal);
}
}
/** Create two peered nodes once, then use send_buffer to inject fuzzing data */
void network_generic_test (const uint8_t * fuzzer_data, size_t fuzzer_data_size)
{
	static std::atomic<bool> initialized{ false };
	if (!initialized.exchange (true))
	{
		nano::force_nano_test_network ();
		std::cout << "Initializing....\n";

		std::signal (SIGINT, nano::signal_handler);
		std::signal (SIGTERM, nano::signal_handler);

		system1 = std::make_shared<nano::system> (24000, 1);
		runner = std::make_shared<nano::thread_runner> (system1->io_ctx, 4);

		assert (0 == system1->nodes[0]->network.size ());
		nano::node_init init1;
		node0 = system1->nodes[0];
		node1 = std::make_shared<nano::node> (init1, system1->io_ctx, 24001, nano::unique_path (), system1->alarm, system1->logging, system1->work);
		node1->start ();
		system1->nodes.push_back (node1);
		node0channel0 = std::make_shared<nano::transport::channel_udp> (system1->nodes[0]->network.udp_channels, node1->network.endpoint ());
		node0->network.send_keepalive (node0channel0);
		network_generic_test (fuzzer_data, fuzzer_data_size);
	}
	else if (fuzzer_data_size > 0)
	{
		auto buffer (std::make_shared<std::vector<uint8_t>> ());
		buffer->reserve (fuzzer_data_size);
		buffer->assign (fuzzer_data, fuzzer_data + fuzzer_data_size);
		++concurrent_calls;
		node0channel0->send_buffer (buffer, nano::stat::detail::all, [buffer](boost::system::error_code const & ec, size_t size_a) {
			--concurrent_calls;
		});
		// Emulate the default IO threads load
		while (concurrent_calls > 4)
		{
			std::this_thread::yield ();
		}
	}
}

/** Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput (const uint8_t * fuzzer_data, size_t fuzzer_data_size)
{
	network_generic_test (fuzzer_data, fuzzer_data_size);
	return 0;
}
