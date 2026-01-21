#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/store/transaction.hpp>

namespace nano::store
{
/**
 * Traits to customize key handling for crawler iteration.
 * The default implementation assumes the iterator key is directly seekable.
 * Specialize this template for compound keys where seeking uses a subset of the key.
 */
template <typename Key, typename Value>
struct crawler_traits
{
	using seek_key_type = Key;

	// Create an iterator key from a seek key (for begin() calls)
	static Key make_iterator_key (seek_key_type const & seek_key)
	{
		return seek_key;
	}

	// Extract the group key from an iterator key
	static seek_key_type group_key (Key const & key)
	{
		return key;
	}

	// Compute the next group key
	static seek_key_type next_group (seek_key_type const & current)
	{
		return inc_sat (comparable (current));
	}

	// Get comparable value from key (for skip_to ordering)
	static auto comparable (seek_key_type const & val)
	{
		return val.number ();
	}
};

/**
 * Database cursor optimized for sequential scans with occasional random access.
 *
 * When processing sorted data (e.g., frontier lists from peers), consecutive keys are
 * often close together in the database. The crawler exploits this locality: both next()
 * and skip_to() first try a small number of sequential iterator increments. If the target
 * isn't found within that window, they fall back to an expensive database seek operation.
 *
 * For compound keys (e.g., pending table's account+hash), entries can be grouped by
 * a key prefix via crawler_traits specialization. The next() method then advances to
 * the next distinct group rather than the next individual entry.
 */
template <typename View>
class crawler
{
public:
	using iterator = typename View::iterator;
	using value_type = typename iterator::value_type;
	using key_type = typename value_type::first_type;
	using mapped_type = typename value_type::second_type;
	using traits = crawler_traits<key_type, mapped_type>;
	using seek_key_type = typename traits::seek_key_type;

	// Number of sequential iterations to try before falling back to seek
	static constexpr size_t sequential_attempts = 10;

public:
	/**
	 * Construct a crawler starting at the given seek key.
	 */
	crawler (View const & view, nano::store::transaction const & transaction, seek_key_type const & start) :
		view_{ view },
		transaction_{ transaction },
		it_{ view_.end (transaction_) },
		end_{ view_.end (transaction_) }
	{
		seek (start);
	}

	// Validity check
	explicit operator bool () const noexcept
	{
		return it_ != end_;
	}

	// Access current entry (precondition: valid)
	value_type const & operator* () const
	{
		release_assert (it_ != end_);
		return *it_;
	}

	value_type const * operator->() const
	{
		release_assert (it_ != end_);
		return &(*it_);
	}

	crawler & operator++ ()
	{
		next ();
		return *this;
	}

	// Get current group key (precondition: valid)
	seek_key_type key () const
	{
		release_assert (it_ != end_);
		return traits::group_key (it_->first);
	}

	// Get current full iterator key (precondition: valid)
	key_type const & full_key () const
	{
		release_assert (it_ != end_);
		return it_->first;
	}

	/**
	 * Seek to first entry >= target.
	 */
	void seek (seek_key_type const & target)
	{
		it_ = view_.begin (transaction_, traits::make_iterator_key (target));
	}

	/**
	 * Move to the next logical entry (as defined by traits::group_key)
	 * Tries sequential iteration before falling back to seek
	 * @return true if still valid after advancing
	 */
	bool next ()
	{
		if (it_ == end_)
		{
			return false;
		}

		auto const starting_key = traits::group_key (it_->first);

		// Try sequential iteration first
		for (size_t count = 0; count < sequential_attempts && it_ != end_; ++count, ++it_)
		{
			if (traits::comparable (traits::group_key (it_->first)) != traits::comparable (starting_key))
			{
				return true;
			}
		}

		if (it_ != end_)
		{
			// Sequential didn't reach next group, do a fresh seek
			auto const next_key = traits::next_group (starting_key);

			if (traits::comparable (next_key) > traits::comparable (starting_key))
			{
				seek (next_key);
			}
			else
			{
				// Saturation: no more groups possible, move to end
				it_ = view_.end (transaction_);
			}
		}

		return it_ != end_;
	}

	/**
	 * Skip to first entry with group key >= target (optimized seek)
	 * Tries sequential iteration before falling back to seek
	 * @return true if valid after skipping
	 */
	bool skip_to (seek_key_type const & target)
	{
		if (it_ == end_)
		{
			return false;
		}

		auto const target_val = traits::comparable (target);

		// Try sequential iteration first
		for (size_t count = 0; count < sequential_attempts && it_ != end_; ++count, ++it_)
		{
			if (traits::comparable (traits::group_key (it_->first)) >= target_val)
			{
				return true;
			}
		}

		// Fall back to direct seek
		seek (target);

		return it_ != end_;
	}

	// Reset to beginning
	void reset ()
	{
		seek (seek_key_type{ 0 });
	}

private:
	View const & view_;
	nano::store::transaction const & transaction_;
	iterator it_;
	iterator const end_;
};
}
