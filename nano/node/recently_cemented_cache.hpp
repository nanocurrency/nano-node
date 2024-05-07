#pragma once

#include <nano/lib/locks.hpp>
#include <nano/node/election_status.hpp>

#include <deque>

namespace nano
{
class container_info_component;
}

namespace nano
{
/*
 * Helper container for storing recently cemented elections (a block from election might be confirmed but not yet cemented by confirmation height processor)
 */
class recently_cemented_cache final
{
public:
	using queue_t = std::deque<nano::election_status>;

	explicit recently_cemented_cache (std::size_t max_size);

	void put (nano::election_status const &);
	queue_t list () const;
	std::size_t size () const;

private:
	queue_t cemented;
	std::size_t const max_size;

	mutable nano::mutex mutex;

public: // Container info
	std::unique_ptr<container_info_component> collect_container_info (std::string const &);
};
}
