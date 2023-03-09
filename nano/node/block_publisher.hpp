#pragma once

#include <memory>

namespace nano
{
class active_transactions;
class block_processor;
class block;

// This class tracks processed blocks to be published.
class block_publisher
{
public:
	block_publisher (nano::active_transactions & active);
	void connect (nano::block_processor & block_processor);

private:
	// Block_processor observer
	void observe (std::shared_ptr<nano::block> block);

	nano::active_transactions & active;
};
}
