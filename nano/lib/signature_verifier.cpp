#include <nano/lib/assert.hpp>
#include <nano/lib/signature_verifier.hpp>
#include <nano/lib/thread_pool.hpp>

#include <algorithm>
#include <latch>

nano::signature_verifier::signature_verifier (unsigned extra_threads_a) :
	extra_threads{ extra_threads_a }
{
}

nano::signature_verifier::~signature_verifier ()
{
	debug_assert (!workers);
}

void nano::signature_verifier::start ()
{
	debug_assert (!workers);
	if (extra_threads > 0)
	{
		workers = std::make_unique<nano::thread_pool> (extra_threads, nano::thread_role::name::signature_verification);
		workers->start ();
	}
}

void nano::signature_verifier::stop ()
{
	if (workers)
	{
		workers->stop ();
		workers.reset ();
	}
}

std::vector<nano::signature_verification> nano::signature_verifier::verify (std::span<nano::signed_message const> batch)
{
	std::vector<std::optional<nano::signature_verification>> slots (batch.size ());
	if (!batch.empty ())
	{
		if (!workers)
		{
			verify_share (batch, slots);
		}
		else
		{
			// Each thread takes a contiguous share of at least min_share entries, the calling thread takes the first one
			auto const shares = std::min<std::size_t> (extra_threads + 1, (batch.size () + min_share - 1) / min_share);
			auto const share_size = (batch.size () + shares - 1) / shares;
			auto const posted = (batch.size () + share_size - 1) / share_size - 1;
			release_assert (share_size > 0 && share_size <= batch.size ());
			release_assert (posted <= extra_threads);

			// A stopped pool drops posted work and would leave the latch waiting forever
			release_assert (workers->alive ());

			// The latch expects exactly one arrival per posted share
			std::latch done{ static_cast<std::ptrdiff_t> (posted) };
			std::size_t posted_count = 0;
			for (std::size_t offset = share_size; offset < batch.size (); offset += share_size)
			{
				auto const count = std::min (share_size, batch.size () - offset);
				workers->post ([messages = batch.subspan (offset, count), verdicts = std::span{ slots }.subspan (offset, count), &done] () {
					verify_share (messages, verdicts);
					done.count_down ();
				});
				++posted_count;
			}
			release_assert (posted_count == posted);

			verify_share (batch.first (share_size), std::span{ slots }.first (share_size));
			done.wait ();
		}
	}

	std::vector<nano::signature_verification> verdicts;
	verdicts.reserve (slots.size ());
	for (auto const & slot : slots)
	{
		release_assert (slot.has_value ());
		verdicts.push_back (*slot);
	}
	return verdicts;
}

void nano::signature_verifier::verify_share (std::span<nano::signed_message const> messages, std::span<std::optional<nano::signature_verification>> verdicts)
{
	release_assert (messages.size () == verdicts.size ());
	for (std::size_t index = 0; index < messages.size (); ++index)
	{
		release_assert (!verdicts[index].has_value ());
		verdicts[index].emplace (nano::signature_verification::verify (messages[index]));
	}
}
