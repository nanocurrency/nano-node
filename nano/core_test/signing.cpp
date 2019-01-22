#include <gtest/gtest.h>

#include <future>
#include <nano/node/node.hpp>

TEST (signature_checker, empty)
{
	nano::signature_checker checker;
	std::promise<void> promise;
	nano::signature_check_set check = { 0, nullptr, nullptr, nullptr, nullptr, nullptr, &promise };
	checker.add (check);
	promise.get_future ().wait ();
}

TEST (signature_checker, many)
{
	nano::keypair key;
	nano::state_block block (key.pub, 0, key.pub, 0, 0, key.prv, key.pub, 0);
	nano::signature_checker checker;
	std::promise<void> promise;
	std::vector<nano::uint256_union> hashes;
	size_t size (1000);
	hashes.reserve (size);
	std::vector<unsigned char const *> messages;
	messages.reserve (size);
	std::vector<size_t> lengths;
	lengths.reserve (size);
	std::vector<unsigned char const *> pub_keys;
	pub_keys.reserve (size);
	std::vector<unsigned char const *> signatures;
	signatures.reserve (size);
	std::vector<int> verifications;
	verifications.resize (size);
	for (auto i (0); i < size; ++i)
	{
		hashes.push_back (block.hash ());
		messages.push_back (hashes.back ().bytes.data ());
		lengths.push_back (sizeof (decltype (hashes)::value_type));
		pub_keys.push_back (block.hashables.account.bytes.data ());
		signatures.push_back (block.signature.bytes.data ());
	}
	nano::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data (), &promise };
	checker.add (check);
	promise.get_future ().wait ();
}

TEST (signature_checker, many_100k_threaded)
{
	nano::keypair key;
	nano::state_block block (key.pub, 0, key.pub, 0, 0, key.prv, key.pub, 0);
	nano::signature_checker checker;
	std::promise<void> promise;
	std::vector<nano::uint256_union> hashes;
	size_t size (100000);
	hashes.reserve (size);
	std::vector<unsigned char const *> messages;
	messages.reserve (size);
	std::vector<size_t> lengths;
	lengths.reserve (size);
	std::vector<unsigned char const *> pub_keys;
	pub_keys.reserve (size);
	std::vector<unsigned char const *> signatures;
	signatures.reserve (size);
	std::vector<int> verifications;
	verifications.resize (size);
	for (auto i (0); i < size; ++i)
	{
		hashes.push_back (block.hash ());
		messages.push_back (hashes.back ().bytes.data ());
		lengths.push_back (sizeof (decltype (hashes)::value_type));
		pub_keys.push_back (block.hashables.account.bytes.data ());
		signatures.push_back (block.signature.bytes.data ());
	}
	nano::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data (), &promise };
	checker.add (check);
	promise.get_future ().wait ();
}

TEST (signature_checker, one)
{
	nano::keypair key;
	nano::state_block block (key.pub, 0, key.pub, 0, 0, key.prv, key.pub, 0);
	nano::signature_checker checker;
	std::promise<void> promise;
	std::vector<nano::uint256_union> hashes;
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
	nano::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data (), &promise };
	checker.add (check);
	promise.get_future ().wait ();
}
