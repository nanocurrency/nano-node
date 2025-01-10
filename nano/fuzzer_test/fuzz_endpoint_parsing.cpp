#include <celerix/node/common.hpp>

/** Fuzz endpoint parsing */
void fuzz_endpoint_parsing (uint8_t const * Data, size_t Size)
{
	auto data (std::string (reinterpret_cast<char *> (const_cast<uint8_t *> (Data)), Size));
	celerix::endpoint endpoint;
	celerix::parse_endpoint (data, endpoint);
	celerix::tcp_endpoint tcp_endpoint;
	celerix::parse_tcp_endpoint (data, tcp_endpoint);
}

/** Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput (uint8_t const * Data, size_t Size)
{
	fuzz_endpoint_parsing (Data, Size);
	return 0;
}
