#include <nano/test_common/signatures.hpp>

nano::signature nano::test::keyless_signature ()
{
	nano::signature result{ 0 };
	result.bytes[0] = 1; // The identity point encodes as y = 1, the scalar half stays zero
	return result;
}
