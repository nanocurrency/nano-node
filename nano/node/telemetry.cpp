#include <nano/lib/stats.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node_observers.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/node/unchecked_map.hpp>
#include <nano/secure/buffer.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/store.hpp>

#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <cstdint>
#include <future>
#include <numeric>
#include <set>

using namespace std::chrono_literals;

nano::telemetry::telemetry (const config & config_a, nano::network & network_a, nano::node_observers & observers_a, nano::network_params & network_params_a, nano::stat & stats_a) :
	config_m{ config_a },
	network{ network_a },
	observers{ observers_a },
	network_params{ network_params_a },
	stats{ stats_a }
{
}

nano::telemetry::~telemetry ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::telemetry::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::telemetry);
		run ();
	});
}

void nano::telemetry::stop ()
{
	stopped = true;
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

bool nano::telemetry::verify (const nano::telemetry_ack & telemetry, const std::shared_ptr<nano::transport::channel> & channel) const
{
	if (telemetry.is_empty_payload ())
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::empty_payload);
		return false;
	}

	// Check if telemetry node id matches channel node id
	if (telemetry.data.node_id != channel->get_node_id ())
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::node_id_mismatch);
		return false;
	}

	// Check whether data is signed by node id presented in telemetry message
	if (telemetry.data.validate_signature ()) // Returns false when signature OK
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::invalid_signature);
		return false;
	}

	// Check for different genesis blocks
	if (telemetry.data.genesis_block != network_params.ledger.genesis->hash ())
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::different_genesis_hash);
		return false;
	}

	return true; // Telemetry is OK
}

void nano::telemetry::process (const nano::telemetry_ack & telemetry, const std::shared_ptr<nano::transport::channel> & channel)
{
	if (!verify (telemetry, channel))
	{
		return;
	}

	nano::unique_lock<nano::mutex> lock{ mutex };

	const auto endpoint = channel->get_endpoint ();

	if (auto it = telemetries.get<tag_endpoint> ().find (endpoint); it != telemetries.get<tag_endpoint> ().end ())
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::update);

		telemetries.get<tag_endpoint> ().modify (it, [&telemetry, &endpoint] (auto & entry) {
			debug_assert (entry.endpoint == endpoint);
			entry.data = telemetry.data;
			entry.last_updated = std::chrono::steady_clock::now ();
		});
	}
	else
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::insert);
		telemetries.get<tag_endpoint> ().insert ({ endpoint, telemetry.data, std::chrono::steady_clock::now (), channel });

		if (telemetries.size () > max_size)
		{
			stats.inc (nano::stat::type::telemetry, nano::stat::detail::overfill);
			telemetries.get<tag_sequenced> ().pop_front (); // Erase oldest entry
		}
	}

	lock.unlock ();

	observers.telemetry.notify (telemetry.data, channel);
}

void nano::telemetry::trigger ()
{
	triggered = true;
	condition.notify_all ();
}

std::size_t nano::telemetry::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return telemetries.size ();
}

void nano::telemetry::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::loop);

		cleanup ();

		if (config_m.enable_ongoing_requests || triggered)
		{
			lock.unlock ();
			triggered = false;
			run_requests ();
			lock.lock ();
		}

		condition.wait_for (lock, network_params.network.telemetry_request_interval);
	}
}

void nano::telemetry::run_requests ()
{
	auto peers = network.list ();

	for (auto & channel : peers)
	{
		request (channel);
	}
}

void nano::telemetry::request (std::shared_ptr<nano::transport::channel> & channel)
{
	stats.inc (nano::stat::type::telemetry, nano::stat::detail::request);

	nano::telemetry_req message{ network_params.network };
	channel->send (message);
}

void nano::telemetry::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	nano::erase_if (telemetries, [this] (entry const & entry) {
		// Remove if telemetry data is stale
		if (!check_timeout (entry))
		{
			stats.inc (nano::stat::type::telemetry, nano::stat::detail::cleanup_outdated);
			return true; // Erase
		}

		// Remove if channel that sent the telemetry is disconnected
		if (!entry.channel->alive ())
		{
			stats.inc (nano::stat::type::telemetry, nano::stat::detail::cleanup_dead);
			return true; // Erase
		}

		return false; // Do not erase
	});
}

bool nano::telemetry::check_timeout (const entry & entry) const
{
	return entry.last_updated + network_params.network.telemetry_cache_cutoff >= std::chrono::steady_clock::now ();
}

std::optional<nano::telemetry_data> nano::telemetry::get_telemetry (const nano::endpoint & endpoint) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	if (auto it = telemetries.get<tag_endpoint> ().find (endpoint); it != telemetries.get<tag_endpoint> ().end ())
	{
		if (check_timeout (*it))
		{
			return it->data;
		}
	}
	return {};
}

std::unordered_map<nano::endpoint, nano::telemetry_data> nano::telemetry::get_all_telemetries () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	std::unordered_map<nano::endpoint, nano::telemetry_data> result;
	for (auto const & entry : telemetries)
	{
		if (check_timeout (entry))
		{
			result[entry.endpoint] = entry.data;
		}
	}
	return result;
}

std::unique_ptr<nano::container_info_component> nano::telemetry::collect_container_info (const std::string & name)
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "telemetries", telemetries.size (), sizeof (decltype (telemetries)::value_type) }));
	return composite;
}

/*
 * old
 */

nano::telemetry_data nano::consolidate_telemetry_data (std::vector<nano::telemetry_data> const & telemetry_datas)
{
	if (telemetry_datas.empty ())
	{
		return {};
	}
	else if (telemetry_datas.size () == 1)
	{
		// Only 1 element in the collection, so just return it.
		return telemetry_datas.front ();
	}

	std::unordered_map<uint8_t, int> protocol_versions;
	std::unordered_map<std::string, int> vendor_versions;
	std::unordered_map<uint64_t, int> bandwidth_caps;
	std::unordered_map<nano::block_hash, int> genesis_blocks;

	// Use a trimmed average which excludes the upper and lower 10% of the results
	std::multiset<uint64_t> account_counts;
	std::multiset<uint64_t> block_counts;
	std::multiset<uint64_t> cemented_counts;
	std::multiset<uint32_t> peer_counts;
	std::multiset<uint64_t> unchecked_counts;
	std::multiset<uint64_t> uptimes;
	std::multiset<uint64_t> bandwidths;
	std::multiset<uint64_t> timestamps;
	std::multiset<uint64_t> active_difficulties;

	for (auto const & telemetry_data : telemetry_datas)
	{
		account_counts.insert (telemetry_data.account_count);
		block_counts.insert (telemetry_data.block_count);
		cemented_counts.insert (telemetry_data.cemented_count);

		std::ostringstream ss;
		ss << telemetry_data.major_version << "." << telemetry_data.minor_version << "." << telemetry_data.patch_version << "." << telemetry_data.pre_release_version << "." << telemetry_data.maker;
		++vendor_versions[ss.str ()];
		timestamps.insert (std::chrono::duration_cast<std::chrono::milliseconds> (telemetry_data.timestamp.time_since_epoch ()).count ());
		++protocol_versions[telemetry_data.protocol_version];
		peer_counts.insert (telemetry_data.peer_count);
		unchecked_counts.insert (telemetry_data.unchecked_count);
		uptimes.insert (telemetry_data.uptime);
		// 0 has a special meaning (unlimited), don't include it in the average as it will be heavily skewed
		if (telemetry_data.bandwidth_cap != 0)
		{
			bandwidths.insert (telemetry_data.bandwidth_cap);
		}

		++bandwidth_caps[telemetry_data.bandwidth_cap];
		++genesis_blocks[telemetry_data.genesis_block];
		active_difficulties.insert (telemetry_data.active_difficulty);
	}

	// Remove 10% of the results from the lower and upper bounds to catch any outliers. Need at least 10 responses before any are removed.
	auto num_either_side_to_remove = telemetry_datas.size () / 10;

	auto strip_outliers_and_sum = [num_either_side_to_remove] (auto & counts) {
		if (num_either_side_to_remove * 2 >= counts.size ())
		{
			return nano::uint128_t (0);
		}
		counts.erase (counts.begin (), std::next (counts.begin (), num_either_side_to_remove));
		counts.erase (std::next (counts.rbegin (), num_either_side_to_remove).base (), counts.end ());
		return std::accumulate (counts.begin (), counts.end (), nano::uint128_t (0), [] (nano::uint128_t total, auto count) {
			return total += count;
		});
	};

	auto account_sum = strip_outliers_and_sum (account_counts);
	auto block_sum = strip_outliers_and_sum (block_counts);
	auto cemented_sum = strip_outliers_and_sum (cemented_counts);
	auto peer_sum = strip_outliers_and_sum (peer_counts);
	auto unchecked_sum = strip_outliers_and_sum (unchecked_counts);
	auto uptime_sum = strip_outliers_and_sum (uptimes);
	auto bandwidth_sum = strip_outliers_and_sum (bandwidths);
	auto active_difficulty_sum = strip_outliers_and_sum (active_difficulties);

	nano::telemetry_data consolidated_data;
	auto size = telemetry_datas.size () - num_either_side_to_remove * 2;
	consolidated_data.account_count = boost::numeric_cast<decltype (consolidated_data.account_count)> (account_sum / size);
	consolidated_data.block_count = boost::numeric_cast<decltype (consolidated_data.block_count)> (block_sum / size);
	consolidated_data.cemented_count = boost::numeric_cast<decltype (consolidated_data.cemented_count)> (cemented_sum / size);
	consolidated_data.peer_count = boost::numeric_cast<decltype (consolidated_data.peer_count)> (peer_sum / size);
	consolidated_data.uptime = boost::numeric_cast<decltype (consolidated_data.uptime)> (uptime_sum / size);
	consolidated_data.unchecked_count = boost::numeric_cast<decltype (consolidated_data.unchecked_count)> (unchecked_sum / size);
	consolidated_data.active_difficulty = boost::numeric_cast<decltype (consolidated_data.unchecked_count)> (active_difficulty_sum / size);

	if (!timestamps.empty ())
	{
		auto timestamp_sum = strip_outliers_and_sum (timestamps);
		consolidated_data.timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (boost::numeric_cast<uint64_t> (timestamp_sum / timestamps.size ())));
	}

	auto set_mode_or_average = [] (auto const & collection, auto & var, auto const & sum, std::size_t size) {
		auto max = std::max_element (collection.begin (), collection.end (), [] (auto const & lhs, auto const & rhs) {
			return lhs.second < rhs.second;
		});
		if (max->second > 1)
		{
			var = max->first;
		}
		else
		{
			var = (sum / size).template convert_to<std::remove_reference_t<decltype (var)>> ();
		}
	};

	auto set_mode = [] (auto const & collection, auto & var, std::size_t size) {
		auto max = std::max_element (collection.begin (), collection.end (), [] (auto const & lhs, auto const & rhs) {
			return lhs.second < rhs.second;
		});
		if (max->second > 1)
		{
			var = max->first;
		}
		else
		{
			// Just pick the first one
			var = collection.begin ()->first;
		}
	};

	// Use the mode of protocol version and vendor version. Also use it for bandwidth cap if there is 2 or more of the same cap.
	set_mode_or_average (bandwidth_caps, consolidated_data.bandwidth_cap, bandwidth_sum, size);
	set_mode (protocol_versions, consolidated_data.protocol_version, size);
	set_mode (genesis_blocks, consolidated_data.genesis_block, size);

	// Vendor version, needs to be parsed out of the string
	std::string version;
	set_mode (vendor_versions, version, size);

	// May only have major version, but check for optional parameters as well, only output if all are used
	std::vector<std::string> version_fragments;
	boost::split (version_fragments, version, boost::is_any_of ("."));
	debug_assert (version_fragments.size () == 5);
	consolidated_data.major_version = boost::lexical_cast<uint8_t> (version_fragments.front ());
	consolidated_data.minor_version = boost::lexical_cast<uint8_t> (version_fragments[1]);
	consolidated_data.patch_version = boost::lexical_cast<uint8_t> (version_fragments[2]);
	consolidated_data.pre_release_version = boost::lexical_cast<uint8_t> (version_fragments[3]);
	consolidated_data.maker = boost::lexical_cast<uint8_t> (version_fragments[4]);

	return consolidated_data;
}