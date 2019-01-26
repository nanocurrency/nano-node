#include <gtest/gtest.h>

#include <future>
#include <nano/node/node.hpp>

TEST (signature_checker, empty)
{
	nano::signature_checker checker (1);
	std::promise<void> promise;
	nano::signature_check_set check = { 0, nullptr, nullptr, nullptr, nullptr, nullptr, &promise };
	checker.verify (check);
	promise.get_future ().wait ();
}

TEST (signature_checker, bulk_single_thread)
{
	nano::keypair key;
	nano::state_block block (key.pub, 0, key.pub, 0, 0, key.prv, key.pub, 0);
	nano::signature_checker checker (1);
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
	checker.verify (check);
	promise.get_future ().wait ();
	bool all_valid = std::all_of (verifications.cbegin (), verifications.cend (), [](auto verification) { return verification == 1; });
	ASSERT_TRUE (all_valid);
}

TEST (signature_checker, many_multi_threaded)
{
	nano::signature_checker checker (4);

	auto signature_checker_work_func = [&checker]() {
		nano::keypair key;
		nano::state_block block (key.pub, 0, key.pub, 0, 0, key.prv, key.pub, 0);
		auto block_hash = block.hash ();

		constexpr auto num_check_sizes = 18;
		constexpr std::array<size_t, num_check_sizes> check_sizes{ 2048, 256, 1024, 1,
			4096, 512, 2050, 1024, 8092, 513, 17, 1024, 2047, 255, 513, 2049, 1025, 1023 };

		std::array<std::promise<void>, num_check_sizes> promises;
		std::vector<nano::signature_check_set> signature_checker_sets;
		signature_checker_sets.reserve (num_check_sizes);

		// Create containers so everything is kept in scope while the threads work on the signature checks
		std::array<std::vector<unsigned char const *>, num_check_sizes> messages;
		std::array<std::vector<size_t>, num_check_sizes> lengths;
		std::array<std::vector<unsigned char const *>, num_check_sizes> pub_keys;
		std::array<std::vector<unsigned char const *>, num_check_sizes> signatures;
		std::array<std::vector<int>, num_check_sizes> verifications;

		// Populate all the signature check sets
		for (int i = 0; i < num_check_sizes; ++i)
		{
			auto check_size = check_sizes[i];

			messages[i].resize (check_size);
			std::fill (messages[i].begin (), messages[i].end (), block_hash.bytes.data ());

			lengths[i].resize (check_size);
			std::fill (lengths[i].begin (), lengths[i].end (), sizeof (decltype (block_hash)));

			pub_keys[i].resize (check_size);
			std::fill (pub_keys[i].begin (), pub_keys[i].end (), block.hashables.account.bytes.data ());

			signatures[i].resize (check_size);
			std::fill (signatures[i].begin (), signatures[i].end (), block.signature.bytes.data ());

			verifications[i].resize (check_size);

			signature_checker_sets.emplace_back (check_size, messages[i].data (), lengths[i].data (), pub_keys[i].data (), signatures[i].data (), verifications[i].data (), &promises[i]);
			checker.verify (signature_checker_sets[i]);
		}

		// Wait for all signature checks to be finished
		for (int i = 0; i < num_check_sizes; ++i)
		{
			promises[i].get_future ().wait ();
			bool all_valid = std::all_of (verifications[i].cbegin (), verifications[i].cend (), [](auto verification) { return verification == 1; });
			ASSERT_TRUE (all_valid);
		}
	};

	std::thread signature_checker_thread1 (signature_checker_work_func);
	std::thread signature_checker_thread2 (signature_checker_work_func);

	signature_checker_thread1.join ();
	signature_checker_thread2.join ();
}

TEST (signature_checker, one)
{
	nano::keypair key;
	nano::state_block block (key.pub, 0, key.pub, 0, 0, key.prv, key.pub, 0);
	nano::signature_checker checker (1);
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
	checker.verify (check);
	promise.get_future ().wait ();
	ASSERT_EQ (verifications.front (), 1);
}
