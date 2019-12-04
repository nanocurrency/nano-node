#pragma once

#include <nano/lib/numbers.hpp>

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
	bool make (unsigned int, nano::root const &, std::vector<std::pair<std::string, uint16_t>> const &, std::function<void(boost::optional<uint64_t>)> const &, uint64_t, boost::optional<nano::account> const & = boost::none);
	void cancel (nano::root const &, bool const local_stop = false);
	void cleanup_finished ();
	void stop ();

	nano::node & node;
	std::unordered_map<nano::root, std::vector<std::weak_ptr<nano::distributed_work>>> items;
	std::mutex mutex;
	std::atomic<bool> stopped{ false };
};

class seq_con_info_component;
std::unique_ptr<seq_con_info_component> collect_seq_con_info (distributed_work_factory & distributed_work, const std::string & name);
}