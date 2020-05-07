#pragma once

#include <nano/lib/timer.hpp>

#include <condition_variable>
#include <mutex>
#include <unordered_map>

#define USING_NANO_TIMED_LOCKS (NANO_TIMED_LOCKS > 0)

namespace nano
{
class mutex;
extern nano::mutex * mutex_to_filter;
extern nano::mutex mutex_to_filter_mutex;
bool should_be_filtered (const char * name);
bool any_filters_registered ();

enum class mutexes
{
	active,
	alarm,
	block_arrival,
	block_processor,
	block_uniquer,
	blockstore_cache,
	confirmation_height_processor,
	dropped_elections,
	election_winner_details,
	gap_cache,
	network_filter,
	observer_set,
	request_aggregator,
	state_block_signature_verification,
	telemetry,
	vote_generator,
	vote_processor,
	vote_uniquer,
	votes_cache,
	work_pool,
	worker
};

char const * mutex_identifier (mutexes mutex);

class mutex
{
public:
	mutex () = default;
	mutex (const char * name_a)
#if USING_NANO_TIMED_LOCKS
	:
	name (name_a)
#endif
	{
#if USING_NANO_TIMED_LOCKS
		// This mutex should be filtered
		if (name && should_be_filtered (name))
		{
			std::lock_guard guard (mutex_to_filter_mutex);
			mutex_to_filter = this;
		}
#endif
	}

#if USING_NANO_TIMED_LOCKS
	~mutex ()
	{
		// Unfilter this destroyed mutex
		if (name && should_be_filtered (name))
		{
			// Unregister the mutex
			std::lock_guard guard (mutex_to_filter_mutex);
			mutex_to_filter = nullptr;
		}
	}
#endif

	void lock ()
	{
		mutex_m.lock ();
	}

	void unlock ()
	{
		mutex_m.unlock ();
	}

	bool try_lock ()
	{
		return mutex_m.try_lock ();
	}

#if USING_NANO_TIMED_LOCKS
	const char * get_name () const
	{
		return name ? name : "";
	}
#endif

private:
#if USING_NANO_TIMED_LOCKS
	const char * name{ nullptr };
#endif
	std::mutex mutex_m;
};

#if USING_NANO_TIMED_LOCKS
template <typename Mutex>
class lock_guard final
{
public:
	explicit lock_guard (Mutex & mutex_a) :
	guard (mutex_a)
	{
	}

	lock_guard (const lock_guard &) = delete;
	lock_guard & operator= (const lock_guard &) = delete;

private:
	std::lock_guard<Mutex> guard;
};

template <>
class lock_guard<nano::mutex> final
{
public:
	explicit lock_guard (nano::mutex & mutex_a);
	~lock_guard () noexcept;

	lock_guard (const lock_guard &) = delete;
	lock_guard & operator= (const lock_guard &) = delete;

private:
	nano::mutex & mut;
	nano::timer<std::chrono::milliseconds> timer;
};

template <typename Mutex, typename = std::enable_if_t<std::is_same<Mutex, nano::mutex>::value>>
class unique_lock final
{
public:
	unique_lock () = default;
	explicit unique_lock (Mutex & mutex_a);
	unique_lock (unique_lock && other) = delete;
	unique_lock & operator= (unique_lock && other) noexcept;
	~unique_lock () noexcept;
	unique_lock (const unique_lock &) = delete;
	unique_lock & operator= (const unique_lock &) = delete;

	void lock ();
	bool try_lock ();
	void unlock ();
	bool owns_lock () const noexcept;
	explicit operator bool () const noexcept;
	Mutex * mutex () const noexcept;

private:
	Mutex * mut{ nullptr };
	bool owns{ false };

	nano::timer<std::chrono::milliseconds> timer;

	void validate () const;
	void lock_impl ();
};

#else
template <typename Mutex>
using lock_guard = std::lock_guard<Mutex>;

template <typename Mutex>
using unique_lock = std::unique_lock<Mutex>;
#endif

// For consistency wrapping the less well known _any variant which can be used with any lockable type
using condition_variable = std::condition_variable_any;

/** A general purpose monitor template */
template <class T>
class locked
{
public:
	template <typename... Args>
	locked (Args &&... args) :
	obj (std::forward<Args> (args)...)
	{
	}

	struct scoped_lock final
	{
		scoped_lock (locked * owner_a) :
		owner (owner_a)
		{
			owner->mutex.lock ();
		}

		~scoped_lock ()
		{
			owner->mutex.unlock ();
		}

		T * operator-> ()
		{
			return &owner->obj;
		}

		T & get () const
		{
			return owner->obj;
		}

		T & operator* () const
		{
			return get ();
		}

		locked * owner{ nullptr };
	};

	scoped_lock operator-> ()
	{
		return scoped_lock (this);
	}

	T & operator= (T const & other)
	{
		nano::unique_lock lk (mutex);
		obj = other;
		return obj;
	}

	operator T () const
	{
		return obj;
	}

	/** Returns a scoped lock wrapper, allowing multiple calls to the underlying object under the same lock */
	scoped_lock lock ()
	{
		return scoped_lock (this);
	}

private:
	T obj;
	nano::mutex mutex;
};
}
