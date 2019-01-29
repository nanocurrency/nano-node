#pragma once

#include <boost/filesystem.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/trivial.hpp>
#include <cstdint>
#include <nano/lib/errors.hpp>
#include <nano/lib/jsonconfig.hpp>

#define FATAL_LOG_PREFIX "FATAL ERROR: "

namespace nano
{
class logging
{
public:
	logging ();
	nano::error serialize_json (nano::jsonconfig &) const;
	nano::error deserialize_json (bool &, nano::jsonconfig &);
	bool upgrade_json (unsigned, nano::jsonconfig &);
	bool ledger_logging () const;
	bool ledger_duplicate_logging () const;
	bool vote_logging () const;
	bool network_logging () const;
	bool network_message_logging () const;
	bool network_publish_logging () const;
	bool network_packet_logging () const;
	bool network_keepalive_logging () const;
	bool network_node_id_handshake_logging () const;
	bool node_lifetime_tracing () const;
	bool insufficient_work_logging () const;
	bool upnp_details_logging () const;
	bool timing_logging () const;
	bool log_rpc () const;
	bool log_ipc () const;
	bool bulk_pull_logging () const;
	bool callback_logging () const;
	bool work_generation_time () const;
	bool log_to_cerr () const;
	void init (boost::filesystem::path const &);

	bool ledger_logging_value;
	bool ledger_duplicate_logging_value;
	bool vote_logging_value;
	bool network_logging_value;
	bool network_message_logging_value;
	bool network_publish_logging_value;
	bool network_packet_logging_value;
	bool network_keepalive_logging_value;
	bool network_node_id_handshake_logging_value;
	bool node_lifetime_tracing_value;
	bool insufficient_work_logging_value;
	bool log_rpc_value;
	bool log_ipc_value;
	bool bulk_pull_logging_value;
	bool work_generation_time_value;
	bool upnp_details_logging_value;
	bool timing_logging_value;
	bool log_to_cerr_value;
	bool flush;
	uintmax_t max_size;
	uintmax_t rotation_size;
	boost::log::sources::logger_mt log;
	int json_version () const
	{
		return 6;
	}
};
}
