#include <nano/node/ssl/ssl_ptr_helper.hpp>

#include <openssl/asn1.h>
#include <openssl/crypto.h>

namespace nano::ssl::detail
{
void deleteSequence (ASN1_SEQUENCE_ANY * sequence)
{
	sk_ASN1_TYPE_pop_free (sequence, getNoOpDeleter<ASN1_TYPE> ());
}

void deleteBuffer (std::uint8_t * data)
{
	OPENSSL_free (data);
}

}
