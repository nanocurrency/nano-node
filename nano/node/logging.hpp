#pragma once

#include <nano/lib/errors.hpp>

#include <boost/log/detail/config.hpp>
#include <boost/shared_ptr.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>

#define FATAL_LOG_PREFIX "FATAL ERROR: "

namespace boost
{
BOOST_LOG_OPEN_NAMESPACE
namespace sinks
{
	class text_file_backend;

	template <class SinkBackendT>
	class synchronous_sink;
}

BOOST_LOG_CLOSE_NAMESPACE

namespace filesystem
{
	class path;
}
}

namespace nano
{
class tomlconfig;
class logging final
{
public:
	nano::error serialize_toml (nano::tomlconfig &) const;
	nano::error deserialize_toml (nano::tomlconfig &);
	bool ledger_logging () const;
	bool ledger_duplicate_logging () const;
	bool ledger_rollback_logging () const;
	bool vote_logging () const;
	bool rep_crawler_logging () const;
	bool election_fork_tally_logging () const;
	bool election_expiration_tally_logging () const;
	bool network_logging () const;
	bool network_timeout_logging () const;
	bool network_message_logging () const;
	bool network_publish_logging () const;
	bool network_packet_logging () const;
	bool network_keepalive_logging () const;
	bool network_node_id_handshake_logging () const;
	bool network_telemetry_logging () const;
	bool network_rejected_logging () const;
	bool node_lifetime_tracing () const;
	bool insufficient_work_logging () const;
	bool upnp_details_logging () const;
	bool timing_logging () const;
	bool log_ipc () const;
	bool bulk_pull_logging () const;
	bool callback_logging () const;
	bool work_generation_time () const;
	bool active_update_logging () const;
	bool election_result_logging () const;
	bool log_to_cerr () const;
	bool single_line_record () const;
	void init (boost::filesystem::path const &);

	bool ledger_logging_value{ false };
	bool ledger_duplicate_logging_value{ false };
	bool ledger_rollback_logging_value{ false };
	bool vote_logging_value{ false };
	bool rep_crawler_logging_value{ false };
	bool election_fork_tally_logging_value{ false };
	bool election_expiration_tally_logging_value{ false };
	bool network_logging_value{ true };
	bool network_timeout_logging_value{ false };
	bool network_message_logging_value{ false };
	bool network_publish_logging_value{ false };
	bool network_packet_logging_value{ false };
	bool network_keepalive_logging_value{ false };
	bool network_node_id_handshake_logging_value{ false };
	bool network_telemetry_logging_value{ false };
	bool network_rejected_logging_value{ false };
	bool node_lifetime_tracing_value{ false };
	bool insufficient_work_logging_value{ true };
	bool log_ipc_value{ true };
	bool bulk_pull_logging_value{ false };
	bool work_generation_time_value{ true };
	bool upnp_details_logging_value{ false };
	bool timing_logging_value{ false };
	bool active_update_value{ false };
	bool election_result_logging_value{ false };
	bool log_to_cerr_value{ false };
	bool flush{ true };
	uintmax_t max_size{ 128 * 1024 * 1024 };
	uintmax_t rotation_size{ 4 * 1024 * 1024 };
	bool stable_log_filename{ false };
	std::chrono::milliseconds min_time_between_log_output{ 5 };
	bool single_line_record_value{ false };
	static void release_file_sink ();

private:
	static boost::shared_ptr<boost::log::sinks::synchronous_sink<boost::log::sinks::text_file_backend>> file_sink;
	static std::atomic_flag logging_already_added;
};
}
