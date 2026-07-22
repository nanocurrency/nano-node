#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/saturate.hpp>
#include <nano/lib/utility.hpp>
#include <nano/store/transaction.hpp>

#include <chrono>
#include <optional>

namespace nano::store
{
// Smallest key greater than current, or nullopt if current is the maximum value
template <typename Key>
	requires requires (Key const & key) { key.number (); }
std::optional<Key> next_key (Key const & current)
{
	auto const next = inc_sat (current.number ());
	if (next == current.number ())
	{
		return std::nullopt; // Saturated at the maximum value
	}
	return Key{ next };
}

/**
 * Traits to customize group key handling for crawler iteration.
 * The default implementation treats every entry as its own group.
 * Specialize this template for compound keys where entries are grouped by a key prefix.
 * Group keys must compare (==, <=>) consistently with the database byte order.
 */
template <typename Key, typename Value>
struct crawler_traits
{
	using group_key_type = Key;

	// Smallest full iterator key belonging to a group (for seeks)
	static Key lower_bound_key (group_key_type const & group)
	{
		return group;
	}

	// Extract the group key from a full iterator key
	static group_key_type group_key (Key const & key)
	{
		return key;
	}

	// Smallest key of the next group, nullopt when no further group is possible
	static std::optional<group_key_type> next_group_key (group_key_type const & current)
	{
		return next_key (current);
	}
};

/**
 * Database cursor optimized for sequential scans with occasional random access.
 *
 * When processing sorted data (e.g., frontier lists from peers), consecutive keys are often close together in the database.
 * The crawler exploits this locality: group advancement and probing first try a small number of sequential iterator increments and only fall back to an expensive database seek when the target isn't found within that window.
 *
 * For compound keys (e.g., pending table's account+hash), entries are grouped by a key prefix via crawler_traits specialization.
 * operator++/next_entry() and find() operate on raw entries; next_group() and find_group() operate on whole groups.
 * For simple keys every entry is its own group, so both levels coincide.
 */
template <typename View, typename Transaction>
class crawler
{
public:
	using iterator = typename View::iterator;
	using value_type = typename iterator::value_type;
	using key_type = typename value_type::first_type;
	using mapped_type = typename value_type::second_type;
	using traits = crawler_traits<key_type, mapped_type>;
	using group_key_type = typename traits::group_key_type;

	// Number of sequential iterations to try before falling back to seek
	static constexpr size_t sequential_attempts = 10;

public:
	/**
	 * Construct a crawler positioned at the first entry with group key >= start.
	 */
	crawler (View const & view, Transaction & transaction, group_key_type start = {}) :
		view_{ view },
		transaction_{ transaction },
		it_{ view_.end (transaction_) },
		end_{ view_.end (transaction_) }
	{
		seek (start);
	}

	/**
	 * @return true if the crawler is positioned at a valid entry
	 */
	explicit operator bool () const noexcept
	{
		return it_ != end_;
	}

	/**
	 * Access the current entry (precondition: valid).
	 */
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

	/**
	 * @return full key of the current entry (precondition: valid)
	 */
	key_type const & key () const
	{
		release_assert (it_ != end_);
		return it_->first;
	}

	/**
	 * @return group key of the current entry (precondition: valid)
	 */
	group_key_type group_key () const
	{
		release_assert (it_ != end_);
		return traits::group_key (it_->first);
	}

	/**
	 * Advance one raw entry, equivalent to next_entry ().
	 */
	crawler & operator++ ()
	{
		next_entry ();
		return *this;
	}

	/**
	 * Move to the next raw iterator entry, without group skipping.
	 * @return true if still valid after advancing
	 */
	bool next_entry ()
	{
		if (it_ != end_)
		{
			++it_;
		}
		return it_ != end_;
	}

	/**
	 * Move to the first entry of the next group, skipping the remaining entries of the current one.
	 * Tries sequential iteration before falling back to seek.
	 * @return true if still valid after advancing
	 */
	bool next_group ()
	{
		if (it_ == end_)
		{
			return false;
		}

		auto const starting_key = traits::group_key (it_->first);

		// Try sequential iteration first
		for (size_t count = 0; count < sequential_attempts && it_ != end_; ++count, ++it_)
		{
			if (traits::group_key (it_->first) != starting_key)
			{
				return true;
			}
		}

		if (it_ != end_)
		{
			// Sequential didn't reach the next group, do a fresh seek
			if (auto const next = traits::next_group_key (starting_key))
			{
				seek (*next);
			}
			else
			{
				// No group can exist past the maximum group key, move to end
				it_ = view_.end (transaction_);
			}
		}

		return it_ != end_;
	}

	/**
	 * Forward-only probe: advance to the first entry with key >= target.
	 * A target behind the current position never matches and does not move the crawler.
	 * @return pointer to the current entry if its key equals target, nullptr otherwise
	 */
	value_type const * find (key_type const & target)
	{
		if (it_ == end_)
		{
			return nullptr;
		}

		// Try sequential iteration first
		for (size_t count = 0; it_ != end_; ++it_)
		{
			// Never searches backwards, a target before the current position returns nullptr even if it exists
			if (it_->first >= target)
			{
				return match_key (target);
			}
			if (++count >= sequential_attempts)
			{
				break;
			}
		}

		// Sequential iteration ran off the end of the table, every entry was before the target
		if (it_ == end_)
		{
			return nullptr;
		}

		// The window only saw entries before the target, the fallback seek must never move backwards
		debug_assert (it_->first < target);

		// Fall back to direct seek
		it_ = view_.begin (transaction_, target);

		return match_key (target);
	}

	/**
	 * Forward-only probe: advance to the first entry with group key >= target.
	 * A target behind the current group never matches and does not move the crawler.
	 * @return pointer to the current entry if its group key equals target, nullptr otherwise
	 */
	value_type const * find_group (group_key_type const & target)
	{
		if (it_ == end_)
		{
			return nullptr;
		}

		// Try sequential iteration first
		for (size_t count = 0; it_ != end_; ++it_)
		{
			// Never searches backwards, a target before the current position returns nullptr even if it exists
			if (traits::group_key (it_->first) >= target)
			{
				return match_group (target);
			}
			if (++count >= sequential_attempts)
			{
				break;
			}
		}

		// Sequential iteration ran off the end of the table, every group was before the target
		if (it_ == end_)
		{
			return nullptr;
		}

		// The window only saw groups before the target, the fallback seek must never move backwards
		debug_assert (traits::group_key (it_->first) < target);

		// Fall back to direct seek
		seek (target);

		return match_group (target);
	}

	/**
	 * Refresh the stored transaction and re-establish the iterator position.
	 * After refresh, the crawler points to the same entry it was at before, or the next valid entry if the original was deleted.
	 */
	void refresh ()
	{
		// Save the full iterator key for precise position restoration
		std::optional<key_type> saved;
		if (it_ != end_)
		{
			saved = it_->first;
		}

		// Destroy old iterators before refreshing transaction.
		// Cursors must be closed before the transaction commits (LMDB frees cursors on commit).
		// Moving to scoped temporaries ensures proper destruction while epoch is still valid.
		{
			[[maybe_unused]] auto old_it = std::move (it_);
			[[maybe_unused]] auto old_end = std::move (end_);
		}

		transaction_.refresh ();

		// Recreate iterators
		end_ = view_.end (transaction_);
		if (saved)
		{
			it_ = view_.begin (transaction_, *saved);
		}
		else
		{
			it_ = view_.end (transaction_);
		}
	}

	/**
	 * Refresh the transaction if it has been held longer than max_age.
	 * @return true if refresh occurred
	 */
	bool refresh_if_needed (std::chrono::milliseconds max_age = std::chrono::milliseconds{ 500 })
	{
		auto now = std::chrono::steady_clock::now ();
		if (now - transaction_.timestamp () > max_age)
		{
			refresh ();
			return true;
		}
		return false;
	}

private:
	// Seek to the first entry with group key >= target
	void seek (group_key_type const & target)
	{
		it_ = view_.begin (transaction_, traits::lower_bound_key (target));
	}

	// Current entry if its key equals target, nullptr otherwise
	value_type const * match_key (key_type const & target) const
	{
		if (it_ != end_ && it_->first == target)
		{
			return &(*it_);
		}
		return nullptr;
	}

	// Current entry if its group key equals target, nullptr otherwise
	value_type const * match_group (group_key_type const & target) const
	{
		if (it_ != end_ && traits::group_key (it_->first) == target)
		{
			return &(*it_);
		}
		return nullptr;
	}

private:
	View const & view_;
	Transaction & transaction_;
	iterator it_;
	iterator end_;
};
}
