#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/node/blockprocessor.hpp>

#include <memory>
#include <unordered_set>

namespace nano
{
class network;

// This class tracks blocks that originated from this node.
class block_broadcast
{
public:
	block_broadcast (nano::network & network, bool enabled = false);
	// Add batch_processed observer to block_processor if enabled
	void connect (nano::block_processor & block_processor);

private:
	// Block_processor observer
	void observe (nano::block_processor::context const &);

	nano::network & network;
	bool enabled;
};
}
