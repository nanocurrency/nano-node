#include <celerix/node/common.hpp>
#include <celerix/node/testing.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace celerix
{
void force_celerix_dev_network ();
}
namespace
{
std::shared_ptr<celerix::test::system> system0;
std::shared_ptr<celerix::node> node0;

class fuzz_visitor : public celerix::message_visitor
{
public:
	virtual void keepalive (celerix::keepalive const &) override
	{
	}
	virtual void publish (celerix::publish const &) override
	{
	}
	virtual void confirm_req (celerix::confirm_req const &) override
	{
	}
	virtual void confirm_ack (celerix::confirm_ack const &) override
	{
	}
	virtual void bulk_pull (celerix::bulk_pull const &) override
	{
	}
	virtual void bulk_pull_account (celerix::bulk_pull_account const &) override
	{
	}
	virtual void bulk_push (celerix::bulk_push const &) override
	{
	}
	virtual void frontier_req (celerix::frontier_req const &) override
	{
	}
	virtual void node_id_handshake (celerix::node_id_handshake const &) override
	{
	}
	virtual void telemetry_req (celerix::telemetry_req const &) override
	{
	}
	virtual void telemetry_ack (celerix::telemetry_ack const &) override
	{
	}
};
}

/** Fuzz live message parsing. This covers parsing and block/vote uniquing. */
void fuzz_message_parser (uint8_t const * Data, size_t Size)
{
	static bool initialized = false;
	if (!initialized)
	{
		celerix::force_celerix_dev_network ();
		initialized = true;
		system0 = std::make_shared<celerix::test::system> (1);
		node0 = system0->nodes[0];
	}

	fuzz_visitor visitor;
	celerix::message_parser parser (node0->network.filter, node0->block_uniquer, node0->vote_uniquer, visitor, node0->work);
	parser.deserialize_buffer (Data, Size);
}

/** Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput (uint8_t const * Data, size_t Size)
{
	fuzz_message_parser (Data, Size);
	return 0;
}
