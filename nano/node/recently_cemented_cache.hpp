#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/node/election_status.hpp>

#include <deque>

namespace celerix
{
class container_info_component;
}

namespace celerix
{
/*
 * Helper container for storing recently cemented elections (a block from election might be confirmed but not yet cemented by confirmation height processor)
 */
class recently_cemented_cache final
{
public:
	using queue_t = std::deque<celerix::election_status>;

	explicit recently_cemented_cache (std::size_t max_size);

	void put (celerix::election_status const &);
	queue_t list () const;
	std::size_t size () const;

	celerix::container_info container_info () const;

private:
	queue_t cemented;
	std::size_t const max_size;

	mutable celerix::mutex mutex;
};
}
