#include <nano/core_test/testutil.hpp>
#include <nano/node/testing.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace nano
{
void cleanup_test_directories_on_exit ();
void force_nano_test_network ();
}
namespace
{
    std::shared_ptr<nano::system> system1;
    std::shared_ptr<nano::node> node0;
    std::shared_ptr<nano::node> node1;
    std::shared_ptr<nano::transport::channel_udp> node0channel0;
    std::shared_ptr<nano::thread_runner> runner;
    std::atomic<int> concurrent_calls{false};
}

/** Create two peered nodes once, then use send_buffer to inject fuzzing data */
void network_generic_test (const uint8_t * Data, size_t Size)
{
    static bool initialized = false;
    if (!initialized)
    {
        nano::force_nano_test_network ();
        initialized = true;
        std::cout << "Initializing....\n";

        auto nano_fuzzer_network = std::getenv ("NANO_FUZZER_NETWORK");
        if (nano_fuzzer_network)
        {
            std::cout << "Fuzzing network\n";
        }
        else
        {
            std::cerr << "No fuzzer environment flag set. Using NANO_FUZZER_NETWORK\n";
        }

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
    }

    if (Size > 0)
    {
        std::shared_ptr<std::vector<uint8_t>> buffer0;
            buffer0 = std::make_shared<std::vector<uint8_t>>();
            buffer0->reserve (Size);
        buffer0->assign (Data, Data + Size);
        ++concurrent_calls;
        node0channel0->send_buffer(buffer0, nano::stat::detail::all, [buffer0](boost::system::error_code const & ec, size_t size_a) {
            --concurrent_calls;
        });
        while (concurrent_calls > 4)
        {
            std::this_thread::yield ();
        }
    }
}

/** Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput (const uint8_t * Data, size_t Size)
{
    network_generic_test (Data, Size);

	//nano::cleanup_test_directories_on_exit ();
	return 0;
}

#ifdef CUSTOM_MUTATOR

// Forward-declare the libFuzzer's mutator callback.
extern "C" size_t
LLVMFuzzerMutate (uint8_t * Data, size_t Size, size_t MaxSize);

// The custom mutator:
//   * deserialize the data (in this case, uncompress).
//     * If the data doesn't deserialize, create a properly serialized dummy.
//   * Mutate the deserialized data (in this case, just call LLVMFuzzerMutate).
//   * Serialize the mutated data (in this case, compress).
extern "C" size_t LLVMFuzzerCustomMutator (uint8_t * Data, size_t Size,
size_t MaxSize, unsigned int Seed)
{
	uint8_t Uncompressed[100];
	size_t UncompressedLen = sizeof (Uncompressed);
	size_t CompressedLen = MaxSize;
	if (Z_OK != uncompress (Uncompressed, &UncompressedLen, Data, Size))
	{
		// The data didn't uncompress.
		// So, it's either a broken input and we want to ignore it,
		// or we've started fuzzing from an empty corpus and we need to supply
		// out first properly compressed input.
		uint8_t Dummy[] = { 'H', 'i' };
		if (Z_OK != compress (Data, &CompressedLen, Dummy, sizeof (Dummy)))
			return 0;
		// fprintf(stderr, "Dummy: max %zd res %zd\n", MaxSize, CompressedLen);
		return CompressedLen;
	}
	UncompressedLen = LLVMFuzzerMutate (Uncompressed, UncompressedLen, sizeof (Uncompressed));
	if (Z_OK != compress (Data, &CompressedLen, Uncompressed, UncompressedLen))
		return 0;
	return CompressedLen;
}

#endif // CUSTOM_MUTATOR
