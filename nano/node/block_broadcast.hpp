#pragma once

#include <nano/lib/blocks.hpp>

#include <memory>
#include <unordered_set>

namespace nano
{
class block_arrival;
class block_processor;
class network;
// This class tracks blocks that originated from this node.
class block_broadcast
{
public:
	block_broadcast (nano::network & network, nano::block_arrival & block_arrival);
	// Add batch_processed observer to block_processor if enabled
	void connect (nano::block_processor & block_processor, bool enabled);
	// Block_processor observer
	void observe (std::shared_ptr<nano::block> block);
	// Mark a block as originating locally
	void set_local (std::shared_ptr<nano::block> block);
	void erase (std::shared_ptr<nano::block> block);

private:
	nano::network & network;
	nano::block_arrival & block_arrival;
	std::unordered_set<std::shared_ptr<nano::block>> local; // Blocks originated on this node
	nano::mutex mutex;
};
}
