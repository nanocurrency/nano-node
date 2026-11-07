#include <nano/lib/assert.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/signature_verification.hpp>

nano::signature_verification::signature_verification (nano::signed_message const & message_a, bool valid_a) :
	message_m{ message_a },
	valid_m{ valid_a }
{
}

nano::signature_verification nano::signature_verification::verify (nano::signed_message const & message)
{
	return { message, !nano::validate_message (message.signer, message.message, message.signature) };
}

nano::signature_verification nano::signature_verification::dummy (nano::signed_message const & message, bool valid)
{
	release_assert (nano::is_dev_run ());
	return { message, valid };
}

bool nano::signature_verification::covers (nano::signed_message const & message) const
{
	return message_m == message;
}

nano::signed_message const & nano::signature_verification::subject () const
{
	return message_m;
}

bool nano::signature_verification::valid () const
{
	return valid_m;
}
