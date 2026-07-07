#pragma once

#include <nano/lib/assert.hpp>
#include <nano/node/block_source.hpp>
#include <nano/secure/common.hpp>

#include <any>
#include <future>

namespace nano
{
class block_context
{
public:
	using result_t = nano::block_status;
	using callback_t = std::function<void (result_t)>;

public: // Keep fields public for simplicity
	std::shared_ptr<nano::block> block;
	nano::block_source source;
	callback_t callback;
	std::chrono::steady_clock::time_point arrival{ std::chrono::steady_clock::now () };
	std::any tag; // Opaque per-block tag from the submitter
	// Result of parallel signature pre-validation, consumed by ledger processing
	nano::signature_verification verified{ nano::signature_verification::unknown };

public:
	block_context (std::shared_ptr<nano::block> block, nano::block_source source, callback_t callback = nullptr, std::any tag = {}) :
		block{ std::move (block) },
		source{ source },
		callback{ std::move (callback) },
		tag{ std::move (tag) }
	{
		debug_assert (source != nano::block_source::unknown);
	}

	std::future<result_t> get_future ()
	{
		return promise.get_future ();
	}

	void set_result (result_t result)
	{
		promise.set_value (result);
	}

private:
	std::promise<result_t> promise;
};
}
