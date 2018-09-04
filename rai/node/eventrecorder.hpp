#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

#include <boost/property_tree/ptree.hpp>
#include <rai/lib/blocks.hpp>
#include <rai/lib/errors.hpp>
#include <rai/node/common.hpp>
#include <rai/node/lmdb.hpp>
#include <rai/secure/utility.hpp>

#ifdef __clang__
#define BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED 1
#endif
#include <boost/crc.hpp>
#include <boost/stacktrace.hpp>

using namespace std::chrono;

namespace nano
{
/** Event recorder error codes */
enum class error_eventrecorder
{
	generic = 1,
	store_open,
	cursor_open,
	no_matching_database,
	serialization,
	deserialization
};

namespace events
{
	/** Type of recording event */
	enum class type : uint8_t
	{
		invalid,
		bootstrap_bulk_push_send,
		bootstrap_bulk_push_receive,
		bootstrap_pull_receive,
		bootstrap_pull_send,
		confirm_req_in,
		confirm_req_out,
		confirm_ack_in,
		confirm_ack_out,
		ledger_processed,
		block_observer_called,
		fork_ledger,
		fork_contender,
		fork_root,
		gap_previous,
		gap_source,
		bad_block_position,
		publish_in,
		publish_out,
		rollback_loser,
		rollback_winner,
		transaction,
		stacktrace,
	};

	/**
 	 * Serialize and deserialize the recorder node from config.json
 	 * All configuration values have defaults.
 	 */
	class recorder_config
	{
	public:
		/** Reads the JSON eventrecorder node */
		bool deserialize_json (boost::property_tree::ptree & tree_a);
		/** True if the event recorder is enabled */
		bool enabled{ false };
		/** If true, record transactions along with stack traces */
		bool record_transactions{ false };
		/** If true, record stack traces for supported events. This adds significant time overhead to recording. */
		bool record_stacktraces{ false };
	};

	/** Base type for all events */
	class event
	{
	public:
		explicit event (nano::events::type type);

		/** Clones the event by delegating to the overriding class' copy constructor */
		virtual std::unique_ptr<event> clone () = 0;

		/** Returns a generated description of the event */
		virtual std::string describe () = 0;
		virtual expected<std::vector<uint8_t>, std::error_code> serialize () = 0;
		virtual std::error_code deserialize (rai::stream & input) = 0;
		virtual std::vector<uint8_t> serialize_key () = 0;
		virtual std::error_code deserialize_key (rai::stream & input) = 0;

		/** Returns a summary string shared by all subclasses */
		virtual std::string summary_string (size_t indent);

		inline uint64_t timestamp_get () const
		{
			return timestamp;
		}
		inline void timestamp_set (uint64_t timestamp)
		{
			this->timestamp = timestamp;
		}
		inline uint32_t ordinal_get () const
		{
			return ordinal;
		}
		inline void ordinal_set (uint32_t ordinal)
		{
			this->ordinal = ordinal;
		}
		inline nano::events::type type_get () const
		{
			return type;
		}

	protected:
		nano::events::type type{ nano::events::type::invalid };
		uint64_t timestamp{ 0 };
		uint32_t ordinal{ 0 };
	};

	/**
	 * A persistent stack trace keyed by its hash. stacktrace_event isn't used on
	 * its own, but other events refer to it in order to record a stacktrace. It's
	 * defined as an event in order to be able to participate in queued persistence
	 * and queries.
	 */
	class stacktrace_event : public event
	{
	public:
		stacktrace_event (nano::events::type type) :
		event (type)
		{
		}

		stacktrace_event (stacktrace_event const & other) :
		event (nano::events::type::stacktrace)
		{
			strace = other.strace;
			strace_hash = other.strace_hash;
		}

		stacktrace_event (uint64_t strace_hash_a) :
		event (nano::events::type::stacktrace), strace_hash (strace_hash_a)
		{
		}

		stacktrace_event (boost::stacktrace::stacktrace & trace, uint64_t trace_hash) :
		event (nano::events::type::stacktrace)
		{
			strace_hash = trace_hash;

			// Convert trace to string. This is fairly slow; stacktrace logging is thus a config option.
			// std::ostringstream ostr;
			// ostr << trace;
			//strace = ostr.str ();
			strace = boost::stacktrace::detail::to_string (&trace.as_vector ()[0], trace.size ());
		}

		std::unique_ptr<event> clone () override
		{
			return std::make_unique<stacktrace_event> (*this);
		}

		std::string summary_string (size_t indent) override;

		inline std::string strace_get () const
		{
			return strace;
		}

		inline void strace_set (uint64_t strace_a)
		{
			strace = strace_a;
		}

		inline uint64_t strace_hash_get () const
		{
			return strace_hash;
		}

		inline void strace_hash_set (uint64_t strace_hash_a)
		{
			strace_hash = strace_hash_a;
		}

		std::string describe () override
		{
			return "Stacktrace event";
		}

		std::vector<uint8_t> serialize_key () override;
		std::error_code deserialize_key (rai::stream & input) override;
		expected<std::vector<uint8_t>, std::error_code> serialize () override;
		std::error_code deserialize (rai::stream & input) override;

	private:
		std::string strace;
		uint64_t strace_hash;
	};

	/** A database transaction event */
	class tx_event : public event
	{
	public:
		tx_event () :
		event (nano::events::type::transaction)
		{
		}

		tx_event (tx_event const & other) :
		event (other.type)
		{
			timestamp = other.timestamp;
			ordinal = other.ordinal;
			tx_id = other.tx_id;
			tx_is_write = other.tx_is_write;
			tx_is_start = other.tx_is_start;
			strace_hash = other.strace_hash;
		}

		tx_event (uint64_t tx_id, bool tx_is_start, bool tx_is_write, uint64_t strace_hash) :
		event (nano::events::type::transaction), tx_id (tx_id), tx_is_start (tx_is_start), tx_is_write (tx_is_write), strace_hash (strace_hash)
		{
			// Skip current frame from stacktrace
			auto strace = boost::stacktrace::stacktrace (2, 4);
			strace_hash = boost::stacktrace::hash_value (strace);
		}

		std::unique_ptr<event> clone () override
		{
			return std::make_unique<tx_event> (*this);
		}

		std::string describe () override
		{
			return "Transaction event";
		}

		uint64_t tx_id_get () const
		{
			return tx_id;
		}
		bool tx_is_start_get () const
		{
			return tx_is_start;
		}
		bool tx_is_write_get () const
		{
			return tx_is_write;
		}
		uint64_t stacktrace_hash_get () const
		{
			return strace_hash;
		}

		std::string summary_string (size_t indent) override;
		std::vector<uint8_t> serialize_key () override;
		std::error_code deserialize_key (rai::stream & input) override;
		expected<std::vector<uint8_t>, std::error_code> serialize () override;
		std::error_code deserialize (rai::stream & input) override;

	private:
		uint64_t tx_id{ 0 };
		bool tx_is_start{ false };
		bool tx_is_write{ false };
		uint64_t strace_hash;
	};

	/** Base type for block events, containing a block hash and an optional endpoint address */
	class block_event : public event
	{
	public:
		block_event (block_event const & other) :
		event (other.type)
		{
			hash = other.hash;
		}
		block_event (nano::events::type type) :
		event (type)
		{
		}

		block_event (nano::events::type type, rai::block_hash hash) :
		block_event (type, hash, nullptr)
		{
		}

		block_event (nano::events::type type, rai::block_hash hash, boost::asio::ip::address const & address);
		block_event (nano::events::type type, rai::block_hash hash, std::unique_ptr<std::array<uint8_t, 16>> endpoint_bytes) :
		event (type), hash (hash), endpoint_bytes (std::move (endpoint_bytes))
		{
		}
		std::unique_ptr<event> clone () override
		{
			return std::make_unique<block_event> (*this);
		}

		std::string describe () override
		{
			return "Block event";
		}

		/** Returns the hash portion of the key */
		rai::block_hash hash_get () const
		{
			return hash;
		}

		std::string summary_string (size_t indent) override;
		std::vector<uint8_t> serialize_key () override;
		std::error_code deserialize_key (rai::stream & input) override;
		expected<std::vector<uint8_t>, std::error_code> serialize () override;
		std::error_code deserialize (rai::stream & input) override;

	protected:
		rai::block_hash hash;
		std::unique_ptr<std::array<uint8_t, 16>> endpoint_bytes;
	};

	/** 
	 * An event involving two blocks. First hash is key (hash with ordinal suffix), second hash is value.
	 */
	class block_pair_event : public block_event
	{
	public:
		block_pair_event (block_pair_event const & other) :
		block_event (other)
		{
			value = other.value;
		}
		block_pair_event (nano::events::type type, rai::block_hash key = 0, rai::block_hash value = 0) :
		block_event (type, key), value (value)
		{
		}

		std::unique_ptr<event> clone () override
		{
			return std::make_unique<block_pair_event> (*this);
		}

		std::string summary_string (size_t indent) override
		{
			std::string summary = event::summary_string (indent);
			if (type == nano::events::type::gap_previous)
			{
				summary.append (", previous: ");
			}
			else if (type == nano::events::type::gap_source)
			{
				summary.append (", source: ");
			}
			else if (type == nano::events::type::rollback_loser)
			{
				summary.append (", winner: ");
			}
			else if (type == nano::events::type::rollback_winner)
			{
				summary.append (", loser: ");
			}
			else if (type == nano::events::type::bad_block_position)
			{
				summary.append (", predecessor: ");
			}
			else
			{
				summary.append (", ");
			}
			summary.append (value.to_string ());
			return summary;
		}

		std::string describe () override
		{
			return "Block pair event";
		}

		inline rai::block_hash value_get ()
		{
			return value;
		}

		expected<std::vector<uint8_t>, std::error_code> serialize () override;
		std::error_code deserialize (rai::stream & input) override;

	private:
		rai::block_hash value;
	};

	/** 
	 * A fork is recorded under three keys (in different fork_ db's) for
	 * fast lookup on root, ledger and contender hashes.
	 */
	class fork : public block_event
	{
	public:
		fork (fork const & other) :
		block_event (other)
		{
			first = other.first;
			second = other.second;
		}
		fork (nano::events::type type, rai::block_hash hash = 0, rai::block_hash first = 0, rai::block_hash second = 0);

		std::unique_ptr<event> clone () override
		{
			return std::make_unique<fork> (*this);
		}

		std::string summary_string (size_t indent) override
		{
			std::string summary = event::summary_string (indent);
			if (type == nano::events::type::fork_contender)
			{
				summary.append (", ledger: ").append (first.to_string ());
				summary.append (", root: ").append (second.to_string ());
			}
			else if (type == nano::events::type::fork_ledger)
			{
				summary.append (", contender: ").append (first.to_string ());
				summary.append (", root: ").append (second.to_string ());
			}
			else if (type == nano::events::type::fork_root)
			{
				summary.append (", ledger: ").append (first.to_string ());
				summary.append (", contender: ").append (second.to_string ());
			}
			else
			{
				assert (false);
			}
			return summary;
		}

		std::string describe () override
		{
			return "Fork event";
		}

		/** If fork_ledger, this returns the contender, otherwise the ledger hash */
		inline rai::block_hash first_get ()
		{
			return first;
		}
		/** If fork_root, this returns the contender, otherwise the root */
		inline rai::block_hash second_get ()
		{
			return second;
		}

		expected<std::vector<uint8_t>, std::error_code> serialize () override;
		std::error_code deserialize (rai::stream & input) override;

	private:
		rai::block_hash first, second;
	};

	/** Info about a database in events.ldb */
	class db_info
	{
	public:
		db_info (std::string name, MDB_dbi * dbi, std::unique_ptr<event> marshaller) :
		name (name), dbi (dbi), marshaller (std::move (marshaller))
		{
		}
		std::string name;
		MDB_dbi * dbi;
		/** 
		 * This serves as a prototype instance, which is cloned whenever an event object of the 
		 * associated type is needed. This makes event iteration generic.
		 */
		std::unique_ptr<event> marshaller;
	};

	/** Event storage api */
	class store
	{
		friend class recorder;

	public:
		store (){};

		/**
		 * Open the event store
		 * @param path_a Path to the event database
		 * @returns error_eventrecorder::store_open if the path is invalid or a db error occurs
		 */
		std::error_code open (boost::filesystem::path const & path_a);
		/** Add an event to the store */
		std::error_code put (rai::transaction & transaction_a, nano::events::event & event_a);
		/** Returns the hash of the stack trace */
		expected<uint64_t, std::error_code> put_stacktrace (rai::transaction & transaction_a, boost::stacktrace::stacktrace & trace_a);
		/** Returns the stacktrace for the given stack trace hash, or an empty string if no entry exists for the hash */
		std::string get_stacktrace (rai::transaction & transaction_a, uint64_t strace_hash_a);
		/** Return the database name corresponding to the type */
		std::string type_to_name (nano::events::type type);
		/** Returns the database corresponding to the name, or nullptr if not found */
		db_info * name_to_dbinfo (std::string name);
		/** Iterate table contents */
		std::error_code iterate_table (std::string table_name, std::function<void(db_info * db_info, std::unique_ptr<nano::events::event>)> callback);

	private:
		std::unique_ptr<rai::mdb_env> environment;
		MDB_dbi * type_to_dbi (nano::events::type type);

		/** Associate db_info with event type for fast lookup */
		void enlist_db (std::unique_ptr<db_info> info)
		{
			dbmap[static_cast<uint8_t> (info->marshaller->type_get ())] = std::move (info);
		}

		/**
		 * Iterates all hash entries in all event tables, invoking the callback for each entry
		 */
		std::error_code iterate_hash (rai::block_hash hash, std::function<void(db_info * db_info, std::unique_ptr<nano::events::event>)> callback);

		/** 
		 * On startup, this is set to the number of event entries (this works because we never delete events).
		 * Used to set the event ordinal.
		 */
		std::atomic<uint32_t> counter{ 0 };

		/** Maps event types to db_info objects */
		std::unordered_map<uint8_t, std::unique_ptr<db_info>, std::hash<uint8_t>> dbmap;

		MDB_dbi bootstrap_bulk_push_send;
		MDB_dbi bootstrap_bulk_push_receive;
		MDB_dbi bootstrap_pull_receive;
		MDB_dbi bootstrap_pull_send;
		MDB_dbi publish_in;
		MDB_dbi publish_out;
		MDB_dbi confirm_req_in;
		MDB_dbi confirm_req_out;
		MDB_dbi confirm_ack_in;
		MDB_dbi confirm_ack_out;
		MDB_dbi ledger_processed;
		MDB_dbi block_observer_called;
		MDB_dbi fork_ledger;
		MDB_dbi fork_contender;
		MDB_dbi fork_root;
		MDB_dbi gap_previous;
		MDB_dbi gap_source;
		MDB_dbi bad_block_position;
		MDB_dbi rollback_loser;
		MDB_dbi rollback_winner;
		MDB_dbi transaction;

		/**
		 * Stacktraces keyed by its CRC. This allows events to reference potentially
		 * large stack traces, while maintaining a low footprint (in practice, there's a limited
		 * amount of unique stack traces)
		*/
		MDB_dbi stacktrace;
	};

	/** Most recent events first*/
	struct event_cmp
	{
		bool operator() (const std::unique_ptr<::nano::events::event> & lhs, const std::unique_ptr<::nano::events::event> & rhs) const
		{
			return lhs->ordinal_get () > rhs->ordinal_get ();
		}
	};
	class recorder;

	/** Event summary for a given hash */
	class summary
	{
	public:
		enum class display_type
		{
			full,
			short_log
		};

		summary (nano::events::recorder & recorder, rai::block_hash hash) :
		recorder (recorder), hash (hash)
		{
		}

		summary (summary && other) :
		recorder (other.recorder)
		{
			events = std::move (other.events);
			hash = other.hash;
		}

		/** Events by type, ordered by ordinal */
		std::map<nano::events::type, std::unique_ptr<std::set<std::unique_ptr<nano::events::event>, event_cmp>>> events;

		/** 
		 * Print to output stream using the given display type 
		 * @param max_items Items to display for each event type, by default 10. Most recent events are display first. Pass std::numeric_limits<size_t>::max () to display all.
		 */
		void print (std::ostream & str, display_type display, size_t max_items = 10);

	private:
		nano::events::recorder & recorder;
		/** Summary is for this hash */
		rai::block_hash hash;
	};

	/** Event recorder api */
	class recorder
	{
	public:
		/**
		 * Construct recorder
		 * @param full_db_path_a Path to database file, such as `application_path_a / "events.ldb"`
		 * @param config_a Configuration object
		 */
		recorder (nano::events::recorder_config config_a, boost::filesystem::path const & full_db_path_a);

		/** Destructor calls stop() */
		~recorder ();

		/** Stop recorder. This flushes the persistence queue. */
		void stop ();

		inline bool enabled ()
		{
			return config.enabled;
		};

		inline store & store_get ()
		{
			return eventstore;
		}

		/**
		 * Records the three hashes under separate keys for fast lookups 
		 */
		inline std::error_code add_fork (rai::block_hash ledger, rai::block_hash contender, rai::block_hash root)
		{
			std::error_code ec;
			if (enabled ())
			{
				ec = add<nano::events::fork> (nano::events::type::fork_ledger, ledger, contender, root);
				if (!ec)
				{
					ec = add<nano::events::fork> (nano::events::type::fork_contender, contender, ledger, root);
				}
				if (!ec)
				{
					ec = add<nano::events::fork> (nano::events::type::fork_root, root, ledger, contender);
				}
			}
			return ec;
		}

		inline std::error_code add_rollback (rai::block_hash loser, rai::block_hash winner)
		{
			std::error_code ec;
			if (enabled ())
			{
				ec = add<nano::events::block_pair_event> (nano::events::type::rollback_loser, loser, winner);
				if (!ec)
				{
					ec = add<nano::events::block_pair_event> (nano::events::type::rollback_winner, winner, loser);
				}
			}
			return ec;
		}

		inline std::error_code add_tx (uint64_t tx_id, bool tx_is_start, bool tx_is_write)
		{
			std::error_code ec;
			if (enabled () && config.record_transactions)
			{
				uint64_t trace_hash (0);
				if (config.record_stacktraces)
				{
					// Create stack trace (skip current frame) and reference its hash from the tx_event
					auto trace = boost::stacktrace::stacktrace (2, 4);
					trace_hash = boost::stacktrace::hash_value (trace);
					ec = add<nano::events::stacktrace_event> (trace, trace_hash);
				}
				ec = add<nano::events::tx_event> (tx_id, tx_is_start, tx_is_write, trace_hash);
			}
			return ec;
		}

		/** 
		 * Creates an event object of the template type and puts it on the persistence queue.
		 * This is a convenience method to keep the recording call-sites one liners, and is a
		 * no-op if recording is disabled.
		 */
		template <typename T, typename... Args>
		inline std::error_code add (Args &&... args)
		{
			std::error_code ec;
			if (enabled ())
			{
				std::unique_ptr<T> event = std::make_unique<T> (std::forward<Args> (args)...);
				// The position in the global set of events; this is a key suffix, making every key unique.
				event->ordinal_set (eventstore.counter.fetch_add (1));
				ec = enqueue (std::move (event));
			}
			return ec;
		}

		void extracted (rai::transaction & tx);

		/** Put event on persistence queue */
		std::error_code enqueue (std::unique_ptr<nano::events::event> event);

		/** Generate summary for the given hash */
		expected<nano::events::summary, std::error_code> get_summary (rai::block_hash hash);

		/**
		 * Get the most recent instance of the recorder.
		 * @deprecated This is a workaround to make recording available to rai::transaction, and will be removed in a future versions.
		 */
		static nano::events::recorder * instance_get ()
		{
			return last_instance.load ();
		}

	private:
		nano::events::store eventstore;
		static std::atomic<nano::events::recorder *> last_instance;
		/** Configuration object deserialized from config.json */
		nano::events::recorder_config config;
		/** Persistence queue to batch writes in a single transaction */
		std::vector<std::unique_ptr<nano::events::event>> queue;
		/** Protects the persistence queue */
		std::recursive_mutex event_queue_mutex;
		/** Flush queue to disk */
		void flush_queue (rai::transaction & tx);
	};
}
} // ns

REGISTER_ERROR_CODES (nano, error_eventrecorder);
