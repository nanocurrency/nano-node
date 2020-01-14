#include <nano/lib/threading.hpp>
#include <nano/node/bootstrap/bootstrap.hpp>
#include <nano/node/bootstrap/bootstrap_attempt.hpp>
#include <nano/node/bootstrap/bootstrap_lazy.hpp>
#include <nano/node/common.hpp>
#include <nano/node/node.hpp>

#include <boost/format.hpp>

#include <algorithm>

constexpr std::chrono::hours nano::bootstrap_excluded_peers::exclude_time_hours;
constexpr std::chrono::hours nano::bootstrap_excluded_peers::exclude_remove_hours;

nano::bootstrap_initiator::bootstrap_initiator (nano::node & node_a) :
node (node_a)
{
	connections = std::make_shared<nano::bootstrap_connections> (node);
	bootstrap_initiator_threads.push_back (boost::thread ([this]() {
		connections->start_populate_connections ();
	}));
	bootstrap_initiator_threads.push_back (boost::thread ([this]() {
		nano::thread_role::set (nano::thread_role::name::bootstrap_initiator);
		run_bootstrap ();
	}));
	bootstrap_initiator_threads.push_back (boost::thread ([this]() {
		nano::thread_role::set (nano::thread_role::name::bootstrap_initiator);
		run_lazy_bootstrap ();
	}));
	bootstrap_initiator_threads.push_back (boost::thread ([this]() {
		nano::thread_role::set (nano::thread_role::name::bootstrap_initiator);
		run_wallet_bootstrap ();
	}));
}

nano::bootstrap_initiator::~bootstrap_initiator ()
{
	stop ();
}

void nano::bootstrap_initiator::bootstrap (bool force)
{
	nano::unique_lock<std::mutex> lock (mutex);
	if (force && attempt != nullptr)
	{
		attempt->stop ();
		// clang-format off
		condition.wait (lock, [&attempt = attempt, &stopped = stopped] { return stopped || attempt == nullptr; });
		// clang-format on
	}
	if (!stopped && attempt == nullptr)
	{
		node.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::initiate, nano::stat::dir::out);
		attempt = std::make_shared<nano::bootstrap_attempt_legacy> (node.shared ());
		condition.notify_all ();
	}
}

void nano::bootstrap_initiator::bootstrap (nano::endpoint const & endpoint_a, bool add_to_peers, bool frontiers_confirmed)
{
	if (add_to_peers)
	{
		node.network.udp_channels.insert (nano::transport::map_endpoint_to_v6 (endpoint_a), node.network_params.protocol.protocol_version);
	}
	nano::unique_lock<std::mutex> lock (mutex);
	if (!stopped)
	{
		if (attempt != nullptr)
		{
			attempt->stop ();
			// clang-format off
			condition.wait (lock, [&attempt = attempt, &stopped = stopped] { return stopped || attempt == nullptr; });
			// clang-format on
		}
		node.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::initiate, nano::stat::dir::out);
		attempt = std::make_shared<nano::bootstrap_attempt_legacy> (node.shared ());
		if (frontiers_confirmed)
		{
			excluded_peers.remove (nano::transport::map_endpoint_to_tcp (endpoint_a));
		}
		if (!excluded_peers.check (nano::transport::map_endpoint_to_tcp (endpoint_a)))
		{
			connections->add_connection (endpoint_a);
		}
		attempt->frontiers_confirmed = frontiers_confirmed;
		condition.notify_all ();
	}
}

void nano::bootstrap_initiator::bootstrap_lazy (nano::hash_or_account const & hash_or_account_a, bool force, bool confirmed)
{
	{
		nano::unique_lock<std::mutex> lock (mutex);
		if (force && lazy_attempt != nullptr)
		{
			lazy_attempt->stop ();
			// clang-format off
			condition.wait (lock, [&lazy_attempt = lazy_attempt, &stopped = stopped] { return stopped || lazy_attempt == nullptr; });
			// clang-format on
		}
		node.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::initiate_lazy, nano::stat::dir::out);
		if (lazy_attempt == nullptr)
		{
			lazy_attempt = std::make_shared<nano::bootstrap_attempt_lazy> (node.shared (), nano::bootstrap_mode::lazy);
		}
		lazy_attempt->lazy_start (hash_or_account_a, confirmed);
	}
	condition.notify_all ();
}

void nano::bootstrap_initiator::bootstrap_wallet (std::deque<nano::account> & accounts_a)
{
	{
		nano::unique_lock<std::mutex> lock (mutex);
		node.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::initiate_wallet_lazy, nano::stat::dir::out);
		if (wallet_attempt == nullptr)
		{
			wallet_attempt = std::make_shared<nano::bootstrap_attempt_wallet> (node.shared (), nano::bootstrap_mode::wallet_lazy);
		}
		wallet_attempt->wallet_start (accounts_a);
	}
	condition.notify_all ();
}

void nano::bootstrap_initiator::run_bootstrap ()
{
	nano::unique_lock<std::mutex> lock (mutex);
	while (!stopped)
	{
		if (attempt != nullptr)
		{
			lock.unlock ();
			if (attempt->mode == nano::bootstrap_mode::legacy)
			{
				attempt->run ();
			}
			lock.lock ();
			attempt = nullptr;
			condition.notify_all ();
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void nano::bootstrap_initiator::run_lazy_bootstrap ()
{
	nano::unique_lock<std::mutex> lock (mutex);
	while (!stopped)
	{
		if (lazy_attempt != nullptr)
		{
			lock.unlock ();
			if (lazy_attempt->mode == nano::bootstrap_mode::lazy)
			{
				lazy_attempt->lazy_run ();
			}
			lock.lock ();
			lazy_attempt = nullptr;
			condition.notify_all ();
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void nano::bootstrap_initiator::run_wallet_bootstrap ()
{
	nano::unique_lock<std::mutex> lock (mutex);
	while (!stopped)
	{
		if (wallet_attempt != nullptr)
		{
			lock.unlock ();
			if (wallet_attempt->mode == nano::bootstrap_mode::wallet_lazy)
			{
				wallet_attempt->wallet_run ();
			}
			lock.lock ();
			wallet_attempt = nullptr;
			condition.notify_all ();
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void nano::bootstrap_initiator::lazy_requeue (nano::block_hash const & hash_a, nano::block_hash const & previous_a, bool confirmed_a)
{
	nano::unique_lock<std::mutex> lock (mutex);
	if (lazy_attempt != nullptr && lazy_attempt->mode == nano::bootstrap_mode::lazy)
	{
		lazy_attempt->lazy_requeue (hash_a, previous_a, confirmed_a);
	}
}

void nano::bootstrap_initiator::add_observer (std::function<void(bool)> const & observer_a)
{
	nano::lock_guard<std::mutex> lock (observers_mutex);
	observers.push_back (observer_a);
}

bool nano::bootstrap_initiator::in_progress ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	return attempt != nullptr || lazy_attempt != nullptr || wallet_attempt != nullptr;
}

std::shared_ptr<nano::bootstrap_attempt_legacy> nano::bootstrap_initiator::current_attempt ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	return attempt;
}

std::shared_ptr<nano::bootstrap_attempt_lazy> nano::bootstrap_initiator::current_lazy_attempt ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	return lazy_attempt;
}

std::shared_ptr<nano::bootstrap_attempt_wallet> nano::bootstrap_initiator::current_wallet_attempt ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	return wallet_attempt;
}

void nano::bootstrap_initiator::stop ()
{
	if (!stopped.exchange (true))
	{
		{
			nano::lock_guard<std::mutex> guard (mutex);
			if (attempt != nullptr)
			{
				attempt->stop ();
			}
			if (lazy_attempt != nullptr)
			{
				lazy_attempt->stop ();
			}
			if (wallet_attempt != nullptr)
			{
				wallet_attempt->stop ();
			}
		}
		connections->stop ();
		condition.notify_all ();

		for (auto & thread : bootstrap_initiator_threads)
		{
			if (thread.joinable ())
			{
				thread.join ();
			}
		}
	}
}

void nano::bootstrap_initiator::notify_listeners (bool in_progress_a)
{
	nano::lock_guard<std::mutex> lock (observers_mutex);
	for (auto & i : observers)
	{
		i (in_progress_a);
	}
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (bootstrap_initiator & bootstrap_initiator, const std::string & name)
{
	size_t count;
	size_t cache_count;
	size_t excluded_peers_count;
	{
		nano::lock_guard<std::mutex> guard (bootstrap_initiator.observers_mutex);
		count = bootstrap_initiator.observers.size ();
	}
	{
		nano::lock_guard<std::mutex> guard (bootstrap_initiator.cache.pulls_cache_mutex);
		cache_count = bootstrap_initiator.cache.cache.size ();
	}
	{
		nano::lock_guard<std::mutex> guard (bootstrap_initiator.excluded_peers.excluded_peers_mutex);
		excluded_peers_count = bootstrap_initiator.excluded_peers.peers.size ();
	}

	auto sizeof_element = sizeof (decltype (bootstrap_initiator.observers)::value_type);
	auto sizeof_cache_element = sizeof (decltype (bootstrap_initiator.cache.cache)::value_type);
	auto sizeof_excluded_peers_element = sizeof (decltype (bootstrap_initiator.excluded_peers.peers)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "observers", count, sizeof_element }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "pulls_cache", cache_count, sizeof_cache_element }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "excluded_peers", excluded_peers_count, sizeof_excluded_peers_element }));
	return composite;
}

void nano::pulls_cache::add (nano::pull_info const & pull_a)
{
	if (pull_a.processed > 500)
	{
		nano::lock_guard<std::mutex> guard (pulls_cache_mutex);
		// Clean old pull
		if (cache.size () > cache_size_max)
		{
			cache.erase (cache.begin ());
		}
		assert (cache.size () <= cache_size_max);
		nano::uint512_union head_512 (pull_a.account_or_head, pull_a.head_original);
		auto existing (cache.get<account_head_tag> ().find (head_512));
		if (existing == cache.get<account_head_tag> ().end ())
		{
			// Insert new pull
			auto inserted (cache.emplace (nano::cached_pulls{ std::chrono::steady_clock::now (), head_512, pull_a.head }));
			(void)inserted;
			assert (inserted.second);
		}
		else
		{
			// Update existing pull
			cache.get<account_head_tag> ().modify (existing, [pull_a](nano::cached_pulls & cache_a) {
				cache_a.time = std::chrono::steady_clock::now ();
				cache_a.new_head = pull_a.head;
			});
		}
	}
}

void nano::pulls_cache::update_pull (nano::pull_info & pull_a)
{
	nano::lock_guard<std::mutex> guard (pulls_cache_mutex);
	nano::uint512_union head_512 (pull_a.account_or_head, pull_a.head_original);
	auto existing (cache.get<account_head_tag> ().find (head_512));
	if (existing != cache.get<account_head_tag> ().end ())
	{
		pull_a.head = existing->new_head;
	}
}

void nano::pulls_cache::remove (nano::pull_info const & pull_a)
{
	nano::lock_guard<std::mutex> guard (pulls_cache_mutex);
	nano::uint512_union head_512 (pull_a.account_or_head, pull_a.head_original);
	cache.get<account_head_tag> ().erase (head_512);
}

uint64_t nano::bootstrap_excluded_peers::add (nano::tcp_endpoint const & endpoint_a, size_t network_peers_count)
{
	uint64_t result (0);
	nano::lock_guard<std::mutex> guard (excluded_peers_mutex);
	// Clean old excluded peers
	while (peers.size () > 1 && peers.size () > std::min (static_cast<double> (excluded_peers_size_max), network_peers_count * excluded_peers_percentage_limit))
	{
		peers.erase (peers.begin ());
	}
	assert (peers.size () <= excluded_peers_size_max);
	auto existing (peers.get<endpoint_tag> ().find (endpoint_a));
	if (existing == peers.get<endpoint_tag> ().end ())
	{
		// Insert new endpoint
		auto inserted (peers.emplace (nano::excluded_peers_item{ std::chrono::steady_clock::steady_clock::now () + exclude_time_hours, endpoint_a, 1 }));
		(void)inserted;
		assert (inserted.second);
		result = 1;
	}
	else
	{
		// Update existing endpoint
		peers.get<endpoint_tag> ().modify (existing, [&result](nano::excluded_peers_item & item_a) {
			++item_a.score;
			result = item_a.score;
			if (item_a.score == nano::bootstrap_excluded_peers::score_limit)
			{
				item_a.exclude_until = std::chrono::steady_clock::now () + nano::bootstrap_excluded_peers::exclude_time_hours;
			}
			else if (item_a.score > nano::bootstrap_excluded_peers::score_limit)
			{
				item_a.exclude_until = std::chrono::steady_clock::now () + nano::bootstrap_excluded_peers::exclude_time_hours * item_a.score * 2;
			}
		});
	}
	return result;
}

bool nano::bootstrap_excluded_peers::check (nano::tcp_endpoint const & endpoint_a)
{
	bool excluded (false);
	nano::lock_guard<std::mutex> guard (excluded_peers_mutex);
	auto existing (peers.get<endpoint_tag> ().find (endpoint_a));
	if (existing != peers.get<endpoint_tag> ().end () && existing->score >= score_limit)
	{
		if (existing->exclude_until > std::chrono::steady_clock::now ())
		{
			excluded = true;
		}
		else if (existing->exclude_until + exclude_remove_hours * existing->score < std::chrono::steady_clock::now ())
		{
			peers.get<endpoint_tag> ().erase (existing);
		}
	}
	return excluded;
}

void nano::bootstrap_excluded_peers::remove (nano::tcp_endpoint const & endpoint_a)
{
	nano::lock_guard<std::mutex> guard (excluded_peers_mutex);
	peers.get<endpoint_tag> ().erase (endpoint_a);
}
