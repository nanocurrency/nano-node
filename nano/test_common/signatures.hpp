#pragma once

#include <nano/lib/numbers.hpp>

namespace nano::test
{
/**
 * Signature that takes no private key to make: the identity point as its point half and zero as its scalar half.
 * It verifies under a public key of small order whenever the message digest is a multiple of that order,
 * so for every message under the identity key and for about one message in four under the zero key of the burn account
 */
nano::signature keyless_signature ();
}
