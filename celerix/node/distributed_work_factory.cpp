#include <celerix/node/distributed_work.hpp>
#include <celerix/node/distributed_work_factory.hpp>
#include <celerix/node/node.hpp>

celerix::distributed_work_factory::distributed_work_factory (celerix::node & node_a) :
	node (node_a)
{
}

celerix::distributed_work_factory::~distributed_work_factory ()
{
	stop ();
}

bool celerix::distributed_work_factory::make (celerix::work_version const version_a, celerix::root const & root_a, std::vector<std::pair<std::string, uint16_t>> const & peers_a, uint64_t difficulty_a, std::function<void (std::optional<uint64_t>)> const & callback_a, std::optional<celerix::account> const & account_a)
{
	return make (std::chrono::seconds (1), celerix::work_request{ version_a, root_a, difficulty_a, account_a, callback_a, peers_a });
}

bool celerix::distributed_work_factory::make (std::chrono::seconds const & backoff_a, celerix::work_request const & request_a)
{
	bool error_l{ true };
	if (!stopped)
	{
		cleanup_finished ();
		if (node.work_generation_enabled (request_a.peers))
		{
			auto distributed (std::make_shared<celerix::distributed_work> (node, request_a, backoff_a));
			{
				celerix::lock_guard<celerix::mutex> guard (mutex);
				items.emplace (request_a.root, distributed);
			}
			distributed->start ();
			error_l = false;
		}
	}
	return error_l;
}

void celerix::distributed_work_factory::cancel (celerix::root const & root_a)
{
	celerix::lock_guard<celerix::mutex> guard_l (mutex);
	auto root_items_l = items.equal_range (root_a);
	std::for_each (root_items_l.first, root_items_l.second, [] (auto item_l) {
		if (auto distributed_l = item_l.second.lock ())
		{
			// Send work_cancel to work peers and stop local work generation
			distributed_l->cancel ();
		}
	});
	items.erase (root_items_l.first, root_items_l.second);
}

void celerix::distributed_work_factory::cleanup_finished ()
{
	celerix::lock_guard<celerix::mutex> guard (mutex);
	std::erase_if (items, [] (decltype (items)::value_type item) { return item.second.expired (); });
}

void celerix::distributed_work_factory::stop ()
{
	if (!stopped.exchange (true))
	{
		// Cancel any ongoing work
		celerix::lock_guard<celerix::mutex> guard (mutex);
		for (auto & item_l : items)
		{
			if (auto distributed_l = item_l.second.lock ())
			{
				distributed_l->cancel ();
			}
		}
		items.clear ();
	}
}

std::size_t celerix::distributed_work_factory::size () const
{
	celerix::lock_guard<celerix::mutex> guard_l (mutex);
	return items.size ();
}

celerix::container_info celerix::distributed_work_factory::container_info () const
{
	celerix::container_info info;
	info.put ("items", size ());
	return info;
}