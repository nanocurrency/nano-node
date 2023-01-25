#pragma once

#include <nano/lib/numbers.hpp>

#include <atomic>
#include <functional>
#include <unordered_map>
#include <vector>

namespace nano
{
class container_info_component;
class distributed_work;
class node;
class root;
struct work_request;

class distributed_work_factory final
{
public:
	distributed_work_factory (nano::node &);
	~distributed_work_factory ();
	bool make (nano::work_version const, nano::root const &, std::vector<std::pair<std::string, uint16_t>> const &, uint64_t, std::function<void (boost::optional<uint64_t>)> const &, boost::optional<nano::account> const & = boost::none);
	bool make (std::chrono::seconds const &, nano::work_request const &);
	void cancel (nano::root const &);
	void cleanup_finished ();
	void stop ();
	std::size_t size () const;

private:
	std::unordered_multimap<nano::root, std::weak_ptr<nano::distributed_work>> items;

	nano::node & node;
	mutable nano::mutex mutex;
	std::atomic<bool> stopped{ false };

	friend std::unique_ptr<container_info_component> collect_container_info (distributed_work_factory &, std::string const &);
};

std::unique_ptr<container_info_component> collect_container_info (distributed_work_factory & distributed_work, std::string const & name);
}
