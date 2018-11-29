#include <gtest/gtest.h>

#include <future>
#include <rai/node/node.hpp>

TEST (signature_checker, empty)
{
	rai::signature_checker checker;
	std::promise<void> promise;
	rai::signature_check_set check = { 0, nullptr, nullptr, nullptr, nullptr, nullptr, &promise };
	checker.add (check);
	promise.get_future ().wait ();
}

TEST (signature_checker, many)
{
	rai::keypair key;
	rai::state_block block (key.pub, 0, key.pub, 0, 0, key.prv, key.pub, 0);
	rai::signature_checker checker;
	std::promise<void> promise;
	std::vector<rai::uint256_union> hashes;
	std::vector<unsigned char const *> messages;
	std::vector<size_t> lengths;
	std::vector<unsigned char const *> pub_keys;
	std::vector<unsigned char const *> signatures;
	std::vector<int> verifications;
	size_t size (1000);
	verifications.resize (size);
	for (auto i (0); i < size; ++i)
	{
		hashes.push_back (block.hash ());
		messages.push_back (hashes.back ().bytes.data ());
		lengths.push_back (sizeof (decltype (hashes)::value_type));
		pub_keys.push_back (block.hashables.account.bytes.data ());
		signatures.push_back (block.signature.bytes.data ());
	}
	rai::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data (), &promise };
	checker.add (check);
	promise.get_future ().wait ();
}

TEST (signature_checker, one)
{
	rai::keypair key;
	rai::state_block block (key.pub, 0, key.pub, 0, 0, key.prv, key.pub, 0);
	rai::signature_checker checker;
	std::promise<void> promise;
	std::vector<rai::uint256_union> hashes;
	std::vector<unsigned char const *> messages;
	std::vector<size_t> lengths;
	std::vector<unsigned char const *> pub_keys;
	std::vector<unsigned char const *> signatures;
	std::vector<int> verifications;
	size_t size (1);
	verifications.resize (size);
	for (auto i (0); i < size; ++i)
	{
		hashes.push_back (block.hash ());
		messages.push_back (hashes.back ().bytes.data ());
		lengths.push_back (sizeof (decltype (hashes)::value_type));
		pub_keys.push_back (block.hashables.account.bytes.data ());
		signatures.push_back (block.signature.bytes.data ());
	}
	rai::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data (), &promise };
	checker.add (check);
	promise.get_future ().wait ();
}
