#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/lib/numbers_templ.hpp>

#include <atomic>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace celerix
{
class container_info_component;
class distributed_work;
class node;
class root;
struct work_request;

class distributed_work_factory final
{
public:
	distributed_work_factory (celerix::node &);
	~distributed_work_factory ();
	bool make (celerix::work_version const, celerix::root const &, std::vector<std::pair<std::string, uint16_t>> const &, uint64_t, std::function<void (std::optional<uint64_t>)> const &, std::optional<celerix::account> const & = std::nullopt);
	bool make (std::chrono::seconds const &, celerix::work_request const &);
	void cancel (celerix::root const &);
	void cleanup_finished ();
	void stop ();
	std::size_t size () const;
	celerix::container_info container_info () const;

private:
	std::unordered_multimap<celerix::root, std::weak_ptr<celerix::distributed_work>> items;

	celerix::node & node;
	mutable celerix::mutex mutex;
	std::atomic<bool> stopped{ false };
};
}
