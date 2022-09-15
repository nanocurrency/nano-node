#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <nanossl/certificate.hpp>
#include <nanossl/certificate_builder.hpp>
#include <nanossl/key_pair.hpp>

TEST (ssl, simple_test)
{
	nano::test::system system{};
	const auto node = system.add_node ();

	nanossl::key_pair key_pair{};
	key_pair.generate();

	nanossl::certificate_builder certificate_builder{};
	const auto certificate = certificate_builder
							 .with_subject("Test Subject")
							 .with_issuer("Test Issuer")
							 .with_public_key(key_pair)
							 .build();

	EXPECT_EQ("CN=Test Subject", certificate.retrieve_subject());
	EXPECT_EQ("CN=Test Issuer", certificate.retrieve_issuer());
	EXPECT_EQ(key_pair, certificate.retrieve_public_key());
}
