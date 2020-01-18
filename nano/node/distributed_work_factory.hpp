#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/distributed_work.hpp>

#include <boost/optional/optional.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace nano
{
class node;
class distributed_work;
class root;

class distributed_work_factory final
{
public:
	distributed_work_factory (nano::node &);
	~distributed_work_factory ();
	bool make (nano::root const &, std::vector<std::pair<std::string, uint16_t>> const &, std::function<void(boost::optional<uint64_t>)> const &, uint64_t, boost::optional<nano::account> const & = boost::none);
	bool make (std::chrono::seconds const &, nano::work_request const &);
	void cancel (nano::root const &, bool const local_stop = false);
	void cleanup_finished ();
	void stop ();

	nano::node & node;
	std::unordered_map<nano::root, std::vector<std::weak_ptr<nano::distributed_work>>> items;
	std::mutex mutex;
	std::atomic<bool> stopped{ false };
};

class container_info_component;
std::unique_ptr<container_info_component> collect_container_info (distributed_work_factory & distributed_work, const std::string & name);
}