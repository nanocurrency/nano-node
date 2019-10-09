#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/epoch.hpp>
#include <nano/lib/nano_pow.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/streams.hpp>
#include <nano/lib/utility.hpp>

#include <boost/optional.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <thread>

namespace nano
{
// Can hold both legacy & nano pow and is aware of what type it is holding through the variant
class proof_of_work final
{
public:
	proof_of_work () = default;
	proof_of_work (nano::legacy_pow legacy_a);
	proof_of_work (nano::nano_pow nano_pow_a);
	proof_of_work (proof_of_work &&) = default;
	proof_of_work & operator= (proof_of_work &&) = default;
	proof_of_work (const proof_of_work &) = default;
	proof_of_work & operator= (const proof_of_work &) = default;
	proof_of_work & operator++ ();
	operator nano::legacy_pow () const;
	operator nano::nano_pow const & () const;
	bool is_legacy () const;
	bool is_empty () const;
	void deserialize (nano::stream & stream_a, bool is_legacy_a);
	void serialize (nano::stream & stream_a) const;
	size_t get_sizeof () const;
	bool operator== (nano::proof_of_work const & other_a) const;
	boost::variant<nano::legacy_pow, nano::nano_pow> pow;
};

class block;
bool work_validate (nano::root const &, nano::proof_of_work, uint64_t * = nullptr);
bool work_validate (nano::block const &, uint64_t * = nullptr);
uint64_t work_value (nano::root const &, nano::proof_of_work);
class opencl_work;
class work_item final
{
public:
	work_item (nano::root const & item_a, std::function<void(boost::optional<nano::proof_of_work> const &)> const & callback_a, uint64_t difficulty_a, nano::epoch epoch_a) :
	item (item_a), callback (callback_a), difficulty (difficulty_a), epoch (epoch_a)
	{
	}

	nano::root item;
	std::function<void(boost::optional<nano::proof_of_work> const &)> callback;
	uint64_t difficulty;
	nano::epoch epoch;
};
class work_pool final
{
public:
	work_pool (unsigned, std::chrono::nanoseconds = std::chrono::nanoseconds (0), std::function<boost::optional<nano::proof_of_work> (nano::root const &, uint64_t, std::atomic<int> &)> = nullptr);
	~work_pool ();
	void loop (uint64_t);
	void stop ();
	void cancel (nano::root const &);
	void generate (nano::root const &, std::function<void(boost::optional<nano::proof_of_work> const &)>, nano::epoch = nano::epoch::epoch_0);
	void generate (nano::root const &, std::function<void(boost::optional<nano::proof_of_work> const &)>, uint64_t, nano::epoch = nano::epoch::epoch_0);
	boost::optional<nano::proof_of_work> generate (nano::root const &, nano::epoch = nano::epoch::epoch_0);
	boost::optional<nano::proof_of_work> generate (nano::root const &, uint64_t, nano::epoch = nano::epoch::epoch_0);
	size_t size ();
	nano::network_constants network_constants;
	std::atomic<int> ticket;
	bool done;
	std::vector<boost::thread> threads;
	std::list<nano::work_item> pending;
	std::mutex mutex;
	nano::condition_variable producer_condition;
	std::chrono::nanoseconds pow_rate_limiter;
	std::function<boost::optional<nano::proof_of_work> (nano::root const &, uint64_t, std::atomic<int> &)> opencl;
	nano::observer_set<bool> work_observers;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (work_pool & work_pool, const std::string & name);

std::string to_string_hex (nano::proof_of_work const & value_a);
bool from_string_hex (std::string const & value_a, nano::proof_of_work & target_a, nano::epoch epoch_a);
bool from_string_hex (std::string const & value_a, nano::proof_of_work & target_a, bool is_legacy_work_a);
}