#pragma once

#include <nano/lib/numbers.hpp>

#include <atomic>
#include <functional>
#include <optional>
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
	bool make (nano::work_version const, nano::root const &, std::vector<std::pair<std::string, uint16_t>> const &, uint64_t, std::function<void (std::optional<uint64_t>)> const &, std::optional<nano::account> const & = std::nullopt);
	bool make (std::chrono::seconds const &, nano::work_request const &);
	void cancel (nano::root const &);
	void cleanup_finished ();
	void stop ();
	std::size_t size () const;
	nano::container_info container_info () const;

private:
	std::unordered_multimap<nano::root, std::weak_ptr<nano::distributed_work>> items;

	nano::node & node;
	mutable nano::mutex mutex;
	std::atomic<bool> stopped{ false };
};
}
