#include <nano/lib/assert.hpp>
#include <nano/lib/rate_limiting.hpp>

#include <algorithm>
#include <limits>

/*
 * token_bucket
 */

namespace
{
// Tolerance for floating point comparisons, far below one token but above accumulated rounding error
constexpr double epsilon = 1e-6;

double compute_capacity (nano::rate_limit limit)
{
	// Even with tiny burst ratios the bucket must hold at least one token to remain usable
	return std::max (1.0, static_cast<double> (limit.rate) * limit.burst_ratio);
}
}

nano::rate::token_bucket::token_bucket (std::size_t rate_a, double burst_ratio_a, clock::time_point now) :
	token_bucket{ nano::rate_limit{ rate_a, burst_ratio_a }, now }
{
}

nano::rate::token_bucket::token_bucket (nano::rate_limit limit, clock::time_point now) :
	limit_m{ limit },
	capacity_m{ compute_capacity (limit) },
	balance{ capacity_m }, // Start with a full bucket so startup is not throttled
	last_refill{ now }
{
	debug_assert (limit.burst_ratio > 0.0);
}

bool nano::rate::token_bucket::consume (std::size_t tokens, clock::time_point now)
{
	// Rate 0 means limiting is disabled; consuming nothing always succeeds
	if (limit_m.rate == 0 || tokens == 0)
	{
		return true;
	}
	balance = projected (now);
	// Never rewind; `now` may be stale when default arguments are evaluated before the caller's lock
	last_refill = std::max (last_refill, now);
	bool const granted = balance >= required (tokens) - epsilon;
	if (granted)
	{
		balance -= static_cast<double> (tokens); // May overdraw below zero for requests larger than the capacity
	}
	return granted;
}

bool nano::rate::token_bucket::can_consume (std::size_t tokens, clock::time_point now) const
{
	// Rate 0 means limiting is disabled; consuming nothing always succeeds
	if (limit_m.rate == 0 || tokens == 0)
	{
		return true;
	}
	return projected (now) >= required (tokens) - epsilon;
}

std::chrono::milliseconds nano::rate::token_bucket::time_to_consume (std::size_t tokens, clock::time_point now) const
{
	// Rate 0 means limiting is disabled; consuming nothing always succeeds
	if (limit_m.rate == 0 || tokens == 0)
	{
		return {};
	}
	// Already consumable, no wait needed
	auto const deficit = required (tokens) - projected (now);
	if (deficit <= epsilon)
	{
		return {};
	}
	// Round up so that waiting the returned duration is sufficient
	return std::chrono::ceil<std::chrono::milliseconds> (std::chrono::duration<double> (deficit / static_cast<double> (limit_m.rate)));
}

void nano::rate::token_bucket::reset (std::size_t rate_a, double burst_ratio_a, clock::time_point now)
{
	reset (nano::rate_limit{ rate_a, burst_ratio_a }, now);
}

void nano::rate::token_bucket::reset (nano::rate_limit limit, clock::time_point now)
{
	debug_assert (limit.burst_ratio > 0.0);
	// Start full when transitioning from unlimited, otherwise carry the balance over
	auto const carried = limit_m.rate == 0 ? std::numeric_limits<double>::max () : projected (now);
	limit_m = limit;
	capacity_m = compute_capacity (limit);
	balance = std::min (carried, capacity_m);
	last_refill = std::max (last_refill, now);
}

nano::rate_limit nano::rate::token_bucket::limit () const
{
	return limit_m;
}

std::size_t nano::rate::token_bucket::capacity () const
{
	// When limiting is disabled there is no meaningful capacity, report 0 rather than the internal placeholder
	return limit_m.rate == 0 ? 0 : static_cast<std::size_t> (capacity_m);
}

std::size_t nano::rate::token_bucket::available (clock::time_point now) const
{
	// When limiting is disabled the balance is never updated, so it carries no meaningful information
	if (limit_m.rate == 0)
	{
		return 0;
	}
	// An overdrawn (negative) balance is reported as 0 available tokens
	return static_cast<std::size_t> (std::max (0.0, projected (now)));
}

double nano::rate::token_bucket::projected (clock::time_point now) const
{
	// Clamp to protect against timestamps older than the last refill
	auto const elapsed = std::max (0.0, std::chrono::duration_cast<std::chrono::duration<double>> (now - last_refill).count ());
	return std::min (balance + elapsed * static_cast<double> (limit_m.rate), capacity_m);
}

double nano::rate::token_bucket::required (std::size_t tokens) const
{
	// Requests larger than the capacity can never accumulate fully; they are granted when the bucket is full
	return std::min (static_cast<double> (tokens), capacity_m);
}
