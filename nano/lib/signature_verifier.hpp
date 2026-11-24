#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/lib/signature_verification.hpp>

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace nano
{
/**
 * Verifies batches of signed messages, sharing each batch between the calling thread and a pool of extra threads.
 * Without extra threads the calling thread verifies the whole batch itself.
 */
class signature_verifier final
{
public:
	/// Smallest share of a batch worth handing to another thread
	static constexpr std::size_t min_share = 16;

	explicit signature_verifier (unsigned extra_threads);
	~signature_verifier ();

	void start ();
	void stop ();

	/// Verifies every message against its signer, returns the verdicts in batch order once the whole batch is done
	std::vector<nano::signature_verification> verify (std::span<nano::signed_message const>);

private:
	/// Verifies one share of a batch on the current thread into the matching verdict slots
	static void verify_share (std::span<nano::signed_message const>, std::span<std::optional<nano::signature_verification>>);

private:
	unsigned const extra_threads;
	/// Pool of extra verification threads, absent when none are configured
	std::unique_ptr<nano::thread_pool> workers;
};
}
