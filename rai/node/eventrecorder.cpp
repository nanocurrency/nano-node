#include <algorithm>
#include <iostream>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/functional/hash.hpp>
#include <boost/lexical_cast.hpp>
#include <rai/node/eventrecorder.hpp>

std::string nano::error_eventrecorder_messages::message (int ec) const
{
	switch (static_cast<nano::error_eventrecorder> (ec))
	{
		case nano::error_eventrecorder::generic:
			return "Unknown error";
		case nano::error_eventrecorder::store_open:
			return "Could not open event store";
		case nano::error_eventrecorder::cursor_open:
			return "Could not open event store cursor";
		case nano::error_eventrecorder::no_matching_database:
			return "No matching database for the event type";
		case nano::error_eventrecorder::serialization:
			return "Serialization";
		case nano::error_eventrecorder::deserialization:
			return "Deserialization";
	}

	return "Invalid error code";
}

namespace
{
/**
 * Read a length-prefixed string. The template argument determines the length type (big endian),
 * and thus the maximum length.
 */
template <typename T>
bool read_string (rai::stream & stream_a, std::string & value)
{
	static_assert (std::is_integral<T>::value, "Can't write non-integral length prefix");
	T length;
	auto amount_read (stream_a.sgetn (reinterpret_cast<uint8_t *> (&length), sizeof (length)));
	if (amount_read == sizeof (length))
	{
		boost::endian::big_to_native_inplace (length);
		std::vector<uint8_t> vec (length);
		amount_read = stream_a.sgetn (reinterpret_cast<uint8_t *> (&vec[0]), length);
		value = std::string (vec.begin (), vec.end ());
		;
	}
	return amount_read != value.size ();
}

/**
 * Write a length-prefixed string. The template argument determines the length type (big endian),
 * and thus the maximum length.
 */
template <typename T>
void write_string (rai::stream & stream_a, std::string & value)
{
	static_assert (std::is_integral<T>::value, "Can't write non-integral length prefix");
	T length = static_cast<T> (value.size ());
	boost::endian::native_to_big_inplace (length);
	auto amount_written (stream_a.sputn (reinterpret_cast<uint8_t const *> (&length), sizeof (length)));
	assert (amount_written == sizeof (length));
	amount_written = stream_a.sputn (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
	assert (amount_written == value.size ());
}
}

nano::events::event::event (nano::events::type type) :
type (type)
{
	timestamp = duration_cast<milliseconds> (system_clock::now ().time_since_epoch ()).count ();
}

std::string nano::events::event::summary_string (size_t indent)
{
	std::ostringstream ostr;
	ostr << std::string (indent, ' ') << "#" << std::to_string (ordinal_get ()) << ", ";
	char time[64];
	time_t ts = (time_t) (timestamp / 1000);
	auto localtime = std::localtime (&ts);
	std::strftime (time, sizeof (time) - 1, "%m/%d %T", localtime);
	ostr << time << "." << std::setw (3) << std::setfill ('0') << uint64_t (timestamp % 1000);
	return ostr.str ();
}

nano::events::block_event::block_event::block_event (nano::events::type type, rai::block_hash hash, boost::asio::ip::address const & address) :
event (type), hash (hash)
{
	endpoint_bytes = std::make_unique<std::array<uint8_t, 16>> (address.to_v6 ().to_bytes ());
}

std::vector<uint8_t> nano::events::block_event::serialize_key ()
{
	std::vector<uint8_t> vec;
	{
		rai::vectorstream output (vec);
		rai::write (output, hash);
		rai::write (output, ordinal_get ());
	}
	return vec;
}

std::error_code nano::events::block_event::deserialize_key (rai::stream & input_a)
{
	std::error_code ec;
	bool error (false);
	error |= rai::read (input_a, hash);
	error |= rai::read (input_a, ordinal);
	return !error ? ec : nano::error_eventrecorder::deserialization;
}

std::string nano::events::block_event::summary_string (size_t indent)
{
	std::string summary = nano::events::event::summary_string (indent);
	if (endpoint_bytes)
	{
		boost::asio::ip::address_v6 v6 (*endpoint_bytes);
		summary += ", endpoint: " + boost::algorithm::erase_first_copy (v6.to_string (), "::ffff:");
	}
	return summary;
}

expected<std::vector<uint8_t>, std::error_code> nano::events::block_event::serialize ()
{
	std::vector<uint8_t> vec;
	{
		rai::vectorstream output (vec);
		rai::write (output, timestamp_get ());
		uint8_t has_endpoint = endpoint_bytes ? 1 : 0;
		rai::write (output, has_endpoint);
		if (has_endpoint)
		{
			rai::write (output, *endpoint_bytes);
		}
	}
	return std::move (vec);
}

std::error_code nano::events::block_event::deserialize (rai::stream & input_a)
{
	std::error_code ec;
	bool error (false);
	uint64_t timestamp;
	error |= rai::read (input_a, timestamp);
	timestamp_set (timestamp);
	uint8_t has_endpoint;
	error |= rai::read (input_a, has_endpoint);
	if (has_endpoint)
	{
		endpoint_bytes = std::make_unique<std::array<uint8_t, 16>> ();
		rai::read (input_a, *endpoint_bytes);
	}
	return !error ? ec : nano::error_eventrecorder::deserialization;
}

std::string nano::events::stacktrace_event::summary_string (size_t indent)
{
	return std::string (indent, ' ') + "Stacktrace id: " + boost::lexical_cast<std::string> (strace_hash) + "\n" + strace;
}

std::vector<uint8_t> nano::events::stacktrace_event::serialize_key ()
{
	std::vector<uint8_t> vec;
	{
		rai::vectorstream output (vec);
		rai::write (output, strace_hash);
	}
	return vec;
}

std::error_code nano::events::stacktrace_event::deserialize_key (rai::stream & input_a)
{
	std::error_code ec;
	auto error (rai::read (input_a, strace_hash));
	return !error ? ec : nano::error_eventrecorder::deserialization;
}

expected<std::vector<uint8_t>, std::error_code> nano::events::stacktrace_event::serialize ()
{
	std::vector<uint8_t> vec;
	{
		rai::vectorstream output (vec);
		write_string<uint16_t> (output, strace);
	}
	return vec;
}

std::error_code nano::events::stacktrace_event::deserialize (rai::stream & input)
{
	std::error_code ec;
	auto error (read_string<uint16_t> (input, strace));
	return !error ? ec : nano::error_eventrecorder::deserialization;
}

std::string nano::events::tx_event::summary_string (size_t indent)
{
	std::string summary = nano::events::event::summary_string (indent);
	summary += ", txid: " + boost::lexical_cast<std::string> (tx_id);
	if (tx_is_write)
	{
		summary += ", RW";
	}
	else
	{
		summary += ", RO";
	}
	if (tx_is_start)
	{
		summary += ", begin";
	}
	else
	{
		summary += ", commit";
	}
	summary += ", stacktrace id: " + boost::lexical_cast<std::string> (strace_hash);
	return summary;
}

std::vector<uint8_t> nano::events::tx_event::serialize_key ()
{
	std::vector<uint8_t> vec;
	{
		rai::vectorstream output (vec);
		rai::write (output, boost::endian::native_to_big (tx_id));
		rai::write (output, boost::endian::native_to_big (ordinal_get ()));
	}
	return vec;
}

std::error_code nano::events::tx_event::deserialize_key (rai::stream & input_a)
{
	std::error_code ec;
	bool error (false);
	error |= rai::read (input_a, tx_id);
	error |= rai::read (input_a, ordinal);
	boost::endian::big_to_native_inplace (tx_id);
	boost::endian::big_to_native_inplace (ordinal);
	return !error ? ec : nano::error_eventrecorder::deserialization;
}

expected<std::vector<uint8_t>, std::error_code> nano::events::tx_event::serialize ()
{
	std::vector<uint8_t> vec;
	{
		rai::vectorstream output (vec);
		rai::write (output, timestamp_get ());
		rai::write (output, tx_is_start);
		rai::write (output, tx_is_write);
		rai::write (output, strace_hash);
	}
	return std::move (vec);
}

std::error_code nano::events::tx_event::deserialize (rai::stream & input_a)
{
	std::error_code ec;
	bool error (false);
	uint64_t timestamp;
	error |= rai::read (input_a, timestamp);
	timestamp_set (timestamp);
	error |= rai::read (input_a, tx_is_start);
	error |= rai::read (input_a, tx_is_write);
	error |= rai::read (input_a, strace_hash);
	return !error ? ec : nano::error_eventrecorder::deserialization;
}

expected<std::vector<uint8_t>, std::error_code> nano::events::block_pair_event::serialize ()
{
	std::vector<uint8_t> vec;
	{
		rai::vectorstream output (vec);
		rai::write (output, timestamp_get ());
		rai::write (output, value.bytes);
	}
	return std::move (vec);
}

std::error_code nano::events::block_pair_event::deserialize (rai::stream & input_a)
{
	std::error_code ec;
	bool error (false);
	uint64_t timestamp;
	error |= rai::read (input_a, timestamp);
	timestamp_set (timestamp);
	error |= rai::read (input_a, value);
	return !error ? ec : nano::error_eventrecorder::deserialization;
}

nano::events::fork::fork (nano::events::type type, rai::block_hash hash, rai::block_hash first, rai::block_hash second) :
block_event (type, hash), first (first), second (second)
{
}

expected<std::vector<uint8_t>, std::error_code> nano::events::fork::serialize ()
{
	std::vector<uint8_t> vec;
	{
		rai::vectorstream output (vec);
		rai::write (output, timestamp_get ());
		rai::write (output, first.bytes);
		rai::write (output, second.bytes);
	}
	return std::move (vec);
}

std::error_code nano::events::fork::deserialize (rai::stream & input_a)
{
	std::error_code ec;
	bool error (false);
	uint64_t timestamp;
	error |= rai::read (input_a, timestamp);
	timestamp_set (timestamp);
	error |= rai::read (input_a, first);
	error |= rai::read (input_a, second);
	return !error ? ec : nano::error_eventrecorder::deserialization;
}

std::error_code nano::events::store::open (boost::filesystem::path const & path_a)
{
	std::error_code ec;
	bool error (false);
	environment = std::make_unique<rai::mdb_env> (error, path_a, 64, MDB_NOSUBDIR | MDB_NOTLS);
	if (!error)
	{
		enlist_db (std::make_unique<db_info> ("bootstrap_bulk_push_send", &bootstrap_bulk_push_send, std::make_unique<block_event> (type::bootstrap_bulk_push_send)));
		enlist_db (std::make_unique<db_info> ("bootstrap_bulk_push_receive", &bootstrap_bulk_push_receive, std::make_unique<block_event> (type::bootstrap_bulk_push_receive)));
		enlist_db (std::make_unique<db_info> ("bootstrap_pull_receive", &bootstrap_pull_receive, std::make_unique<block_event> (type::bootstrap_pull_receive)));
		enlist_db (std::make_unique<db_info> ("bootstrap_pull_send", &bootstrap_pull_send, std::make_unique<block_event> (type::bootstrap_pull_send)));
		enlist_db (std::make_unique<db_info> ("publish_in", &publish_in, std::make_unique<block_event> (type::publish_in)));
		enlist_db (std::make_unique<db_info> ("publish_out", &publish_out, std::make_unique<block_event> (type::publish_out)));
		enlist_db (std::make_unique<db_info> ("confirm_req_in", &confirm_req_in, std::make_unique<block_event> (type::confirm_req_in)));
		enlist_db (std::make_unique<db_info> ("confirm_req_out", &confirm_req_out, std::make_unique<block_event> (type::confirm_req_out)));
		enlist_db (std::make_unique<db_info> ("confirm_ack_in", &confirm_ack_in, std::make_unique<block_event> (type::confirm_ack_in)));
		enlist_db (std::make_unique<db_info> ("confirm_ack_out", &confirm_ack_out, std::make_unique<block_event> (type::confirm_ack_out)));
		enlist_db (std::make_unique<db_info> ("ledger_processed", &ledger_processed, std::make_unique<block_event> (type::ledger_processed)));
		enlist_db (std::make_unique<db_info> ("block_observer_called", &block_observer_called, std::make_unique<block_event> (type::block_observer_called)));
		enlist_db (std::make_unique<db_info> ("fork_ledger", &fork_ledger, std::make_unique<fork> (type::fork_ledger)));
		enlist_db (std::make_unique<db_info> ("fork_contender", &fork_contender, std::make_unique<fork> (type::fork_contender)));
		enlist_db (std::make_unique<db_info> ("fork_root", &fork_root, std::make_unique<fork> (type::fork_root)));
		enlist_db (std::make_unique<db_info> ("gap_previous", &gap_previous, std::make_unique<block_pair_event> (type::gap_previous)));
		enlist_db (std::make_unique<db_info> ("gap_source", &gap_source, std::make_unique<block_pair_event> (type::gap_source)));
		enlist_db (std::make_unique<db_info> ("bad_block_position", &bad_block_position, std::make_unique<block_pair_event> (type::bad_block_position)));
		enlist_db (std::make_unique<db_info> ("rollback_loser", &rollback_loser, std::make_unique<block_pair_event> (type::rollback_loser)));
		enlist_db (std::make_unique<db_info> ("rollback_winner", &rollback_winner, std::make_unique<block_event> (type::rollback_winner)));
		enlist_db (std::make_unique<db_info> ("transaction", &transaction, std::make_unique<tx_event> ()));
		enlist_db (std::make_unique<db_info> ("stacktrace", &stacktrace, std::make_unique<stacktrace_event> (type::stacktrace)));
		rai::transaction transaction (environment->tx_begin (true));

		bool error (false);
		for (auto & db : dbmap)
		{
			auto & entry = db.second;
			error |= mdb_dbi_open (environment->tx (transaction), entry->name.c_str (), MDB_CREATE, entry->dbi) != 0;
			if (!error)
			{
				MDB_stat stat;
				mdb_stat (environment->tx (transaction), *entry->dbi, &stat);
				counter += stat.ms_entries;
			}
			else
			{
				break;
			}
		}

		if (error != 0)
		{
			ec = nano::error_eventrecorder::store_open;
		}
	}
	return ec;
}

MDB_dbi * nano::events::store::type_to_dbi (nano::events::type type)
{
	MDB_dbi * dbi (nullptr);
	auto match = dbmap.find (static_cast<uint8_t> (type));
	if (match != dbmap.end ())
	{
		dbi = match->second->dbi;
	}
	return dbi;
}

nano::events::db_info * nano::events::store::name_to_dbinfo (std::string name)
{
	db_info * dbinfo (nullptr);
	for (auto & db : dbmap)
	{
		if (db.second->name == name)
		{
			dbinfo = db.second.get ();
			break;
		}
	}
	return dbinfo;
}

std::string nano::events::store::type_to_name (nano::events::type type)
{
	std::string name;
	auto match = dbmap.find (static_cast<uint8_t> (type));
	if (match != dbmap.end ())
	{
		name = match->second->name;
	}
	return name;
}

std::string nano::events::store::get_stacktrace (rai::transaction & transaction_a, uint64_t strace_hash_a)
{
	stacktrace_event event (strace_hash_a);
	auto key_vec (event.serialize_key ());
	rai::mdb_val key (key_vec.size (), key_vec.data ());
	rai::mdb_val data;

	auto status (mdb_get (environment->tx (transaction_a), stacktrace, key, data));
	if (status != MDB_NOTFOUND)
	{
		rai::bufferstream datastream (reinterpret_cast<uint8_t const *> (data.value.mv_data), data.value.mv_size);
		event.deserialize (datastream);
	}

	return event.strace_get ();
}

std::error_code nano::events::store::put (rai::transaction & transaction_a, nano::events::event & event_a)
{
	std::error_code ec;
	auto dbi = type_to_dbi (event_a.type_get ());
	if (!dbi)
	{
		ec = nano::error_eventrecorder::no_matching_database;
	}
	else
	{
		auto buf = event_a.serialize ();
		if (buf)
		{
			auto vec = event_a.serialize_key ();
			rai::mdb_val key (vec.size (), vec.data ());
			auto status (mdb_put (environment->tx (transaction_a), *dbi, key, rai::mdb_val (buf->size (), buf->data ()), 0));
			assert (status == 0);
		}
	}
	return ec;
}

std::error_code nano::events::store::iterate_table (std::string table_name, std::function<void(db_info * db_info, std::unique_ptr<nano::events::event>)> callback)
{
	std::error_code ec;
	auto dbinfo (name_to_dbinfo (table_name));
	if (dbinfo)
	{
		MDB_cursor * cursor (nullptr);
		rai::transaction tx (environment->tx_begin (false));
		auto status (mdb_cursor_open (environment->tx (tx), *dbinfo->dbi, &cursor));
		if (status != MDB_SUCCESS)
		{
			ec = nano::error_eventrecorder::cursor_open;
		}
		else
		{
			rai::mdb_val key, data;
			auto status (mdb_cursor_get (cursor, &key.value, &data.value, MDB_FIRST));
			for (size_t entry = 0; status == MDB_SUCCESS; entry++)
			{
				auto event = dbinfo->marshaller->clone ();
				rai::bufferstream datastream (reinterpret_cast<uint8_t const *> (data.value.mv_data), data.value.mv_size);
				event->deserialize (datastream);

				rai::bufferstream keystream (reinterpret_cast<uint8_t const *> (key.value.mv_data), sizeof (rai::block_hash) + sizeof (uint32_t));
				event->deserialize_key (keystream);

				callback (dbinfo, std::move (event));
				status = mdb_cursor_get (cursor, &key.value, &data.value, MDB_NEXT);
			}
			mdb_cursor_close (cursor);
		}
	}
	return ec;
}

std::error_code nano::events::store::iterate_hash (rai::block_hash hash_a, std::function<void(db_info * db_info, std::unique_ptr<nano::events::event>)> callback)
{
	std::error_code ec;
	rai::transaction tx (environment->tx_begin (false));
	for (auto & db : dbmap)
	{
		MDB_cursor * cursor (nullptr);
		auto status (mdb_cursor_open (environment->tx (tx), *db.second->dbi, &cursor));
		if (status != MDB_SUCCESS)
		{
			ec = nano::error_eventrecorder::cursor_open;
		}
		else
		{
			rai::mdb_val key (hash_a), data;
			auto status (mdb_cursor_get (cursor, &key.value, &data.value, MDB_SET_RANGE));
			for (size_t entry = 0; status == MDB_SUCCESS; entry++)
			{
				if (key.value.mv_size == sizeof (rai::block_hash) + sizeof (uint32_t))
				{
					rai::block_hash hash_l;
					rai::bufferstream stream (reinterpret_cast<uint8_t const *> (key.value.mv_data), sizeof (rai::block_hash));
					rai::read (stream, hash_l);

					if (hash_a == hash_l)
					{
						auto event = db.second->marshaller->clone ();
						rai::bufferstream datastream (reinterpret_cast<uint8_t const *> (data.value.mv_data), data.value.mv_size);
						event->deserialize (datastream);

						rai::bufferstream keystream (reinterpret_cast<uint8_t const *> (key.value.mv_data), sizeof (rai::block_hash) + sizeof (uint32_t));
						event->deserialize_key (keystream);

						callback (db.second.get (), std::move (event));
					}
					else
					{
						break;
					}
				}
				status = mdb_cursor_get (cursor, &key.value, &data.value, MDB_NEXT);
			}
			mdb_cursor_close (cursor);
		}
	}

	return ec;
}

std::atomic<nano::events::recorder *> nano::events::recorder::last_instance (nullptr);

nano::events::recorder::recorder (nano::events::recorder_config config, boost::filesystem::path const & full_db_path_a) :
config (config)
{
	last_instance = this;
	auto path = full_db_path_a;
	if (enabled ())
	{
		eventstore.open (path);
	}
}

nano::events::recorder::~recorder ()
{
	stop ();
}

// This must be called under a lock
void nano::events::recorder::flush_queue (rai::transaction & tx)
{
	while (queue.size () > 0)
	{
		auto & next = queue.back ();
		eventstore.put (tx, *next.get ());
		queue.pop_back ();
	}
}

std::error_code nano::events::recorder::enqueue (std::unique_ptr<nano::events::event> event)
{
	assert (enabled ());
	std::error_code ec;
	static const size_t max_queue_size = 75;
	std::unique_lock<std::recursive_mutex> lock (event_queue_mutex);

	// Flush queue if max queue size is reached. We defer flushing events related to transaction logging since we need
	// a transaction to flush, thus causing a loop.
	if (event->type_get () != nano::events::type::transaction && event->type_get () != nano::events::type::stacktrace && queue.size () >= max_queue_size)
	{
		rai::transaction tx (eventstore.environment->tx_begin (true));
		flush_queue (tx);
	}
	else
	{
		queue.push_back (std::move (event));
	}
	return ec;
}

void nano::events::recorder::stop ()
{
	std::unique_lock<std::recursive_mutex> lock (event_queue_mutex);
	if (queue.size () > 0)
	{
		rai::transaction tx (eventstore.environment->tx_begin (true));
		flush_queue (tx);
	}
}

expected<nano::events::summary, std::error_code> nano::events::recorder::get_summary (rai::block_hash hash)
{
	nano::events::summary summary (*this, hash);

	eventstore.iterate_hash (hash, [&summary](nano::events::db_info * db_info, std::unique_ptr<nano::events::event> event) {
		auto events = summary.events.find (event->type_get ());
		if (events == summary.events.end ())
		{
			summary.events[event->type_get ()] = std::make_unique<std::set<std::unique_ptr<nano::events::event>, nano::events::event_cmp>> ();
		}
		summary.events[event->type_get ()]->insert (std::move (event));
	});

	return std::move (summary);
}

void nano::events::summary::print (std::ostream & stream_a, nano::events::summary::display_type display_a, size_t max_items_a)
{
	const int indent = 4;

	for (auto & entry : events)
	{
		stream_a << recorder.store_get ().type_to_name (entry.first) << std::endl;
		auto & events = *entry.second;
		size_t count = 0;
		size_t excluded = 0;
		for (auto & event : events)
		{
			if (++count >= max_items_a)
			{
				++excluded;
			}
			else
			{
				time_t ts = (time_t)event->timestamp_get ();
				stream_a << event->summary_string (indent) << std::endl;
			}
		}
		if (excluded > 0)
		{
			stream_a << std::string (indent, ' ') << excluded << " more..." << std::endl;
		}
	}
}

bool nano::events::recorder_config::deserialize_json (boost::property_tree::ptree & tree_a)
{
	enabled = tree_a.get<bool> ("enabled", enabled);
	record_transactions = tree_a.get<bool> ("record_transactions", record_transactions);
	record_stacktraces = tree_a.get<bool> ("record_stacktraces", record_stacktraces);
	return false;
}
