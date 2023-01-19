#include <nano/node/block_arrival.hpp>

bool nano::block_arrival::add (nano::block_hash const & hash_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto now (std::chrono::steady_clock::now ());
	auto inserted (arrival.get<tag_sequence> ().emplace_back (nano::block_arrival_info{ now, hash_a }));
	auto result (!inserted.second);
	return result;
}

bool nano::block_arrival::recent (nano::block_hash const & hash_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto now (std::chrono::steady_clock::now ());
	while (arrival.size () > arrival_size_min && arrival.get<tag_sequence> ().front ().arrival + arrival_time_min < now)
	{
		arrival.get<tag_sequence> ().pop_front ();
	}
	return arrival.get<tag_hash> ().find (hash_a) != arrival.get<tag_hash> ().end ();
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (block_arrival & block_arrival, std::string const & name)
{
	std::size_t count = 0;
	{
		nano::lock_guard<nano::mutex> guard{ block_arrival.mutex };
		count = block_arrival.arrival.size ();
	}

	auto sizeof_element = sizeof (decltype (block_arrival.arrival)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "arrival", count, sizeof_element }));
	return composite;
}