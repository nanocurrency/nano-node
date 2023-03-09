#pragma once

#include <memory>

namespace nano
{
class gap_cache;
class block_processor;
class block;

// Observes the processed blocks and tracks them (gap_cache) if they are gap blocks.
class gap_tracker
{
public:
	gap_tracker (nano::gap_cache & gap_cache);
	void connect (nano::block_processor & block_processor);

private:
	// Block_processor observer
	void observe (std::shared_ptr<nano::block> block);

	nano::gap_cache & gap_cache;
};
}
