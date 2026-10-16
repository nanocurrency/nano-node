#pragma once

#include <nano/lib/assert.hpp>
#include <nano/node/block_source.hpp>
#include <nano/secure/common.hpp>

#include <any>
#include <future>
#include <memory>

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

public:
	block_context (std::shared_ptr<nano::block> block, nano::block_source source, callback_t callback = nullptr, std::any tag = {}) :
		block{ std::move (block) },
		source{ source },
		callback{ std::move (callback) },
		tag{ std::move (tag) }
	{
		debug_assert (source != nano::block_source::unknown);
	}

	// Future of the result, which only a blocking submission waits for
	std::future<result_t> get_future ()
	{
		release_assert (promise == nullptr);
		promise = std::make_unique<std::promise<result_t>> ();
		return promise->get_future ();
	}

	void set_result (result_t result)
	{
		if (promise)
		{
			promise->set_value (result);
		}
	}

private:
	std::unique_ptr<std::promise<result_t>> promise; // Made on demand, a promise allocates its shared state and most blocks never need one
};
}
