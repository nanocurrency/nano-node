#include <nano/lib/jsonconfig.hpp>
#include <nano/node/diagnosticsconfig.hpp>

nano::error nano::diagnostics_config::serialize_json (nano::jsonconfig & json) const
{
	nano::jsonconfig txn_tracking_l;
	txn_tracking_l.put ("enable", txn_tracking.enable);
	txn_tracking_l.put ("min_read_txn_time", txn_tracking.min_read_txn_time.count ());
	txn_tracking_l.put ("min_write_txn_time", txn_tracking.min_write_txn_time.count ());
	txn_tracking_l.put ("ignore_writes_below_block_processor_max_time", txn_tracking.ignore_writes_below_block_processor_max_time);
	json.put_child ("txn_tracking", txn_tracking_l);
	return json.get_error ();
}

nano::error nano::diagnostics_config::deserialize_json (nano::jsonconfig & json)
{
	auto txn_tracking_l (json.get_optional_child ("txn_tracking"));
	if (txn_tracking_l)
	{
		txn_tracking_l->get_optional<bool> ("enable", txn_tracking.enable);
		auto min_read_txn_time_l = static_cast<unsigned long> (txn_tracking.min_read_txn_time.count ());
		txn_tracking_l->get_optional ("min_read_txn_time", min_read_txn_time_l);
		txn_tracking.min_read_txn_time = std::chrono::milliseconds (min_read_txn_time_l);

		auto min_write_txn_time_l = static_cast<unsigned long> (txn_tracking.min_write_txn_time.count ());
		txn_tracking_l->get_optional ("min_write_txn_time", min_write_txn_time_l);
		txn_tracking.min_write_txn_time = std::chrono::milliseconds (min_write_txn_time_l);

		txn_tracking_l->get_optional<bool> ("ignore_writes_below_block_processor_max_time", txn_tracking.ignore_writes_below_block_processor_max_time);
	}
	return json.get_error ();
}
