#include <nano/node/signatures.hpp>
#include <nano/secure/common.hpp>

#include <gtest/gtest.h>

TEST (signature_checker, empty)
{
	nano::signature_checker checker (0);
	nano::signature_check_set check = { 0, nullptr, nullptr, nullptr, nullptr, nullptr };
	checker.verify (check);
}

TEST (signature_checker, bulk_single_thread)
{
	nano::keypair key;
	nano::block_builder builder;
	auto block = builder
				 .state ()
				 .account (key.pub)
				 .previous (0)
				 .representative (key.pub)
				 .balance (0)
				 .link (0)
				 .sign (key.prv, key.pub)
				 .work (0)
				 .build ();
	nano::signature_checker checker (0);
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
		hashes.push_back (block->hash ());
		messages.push_back (hashes.back ().bytes.data ());
		lengths.push_back (sizeof (decltype (hashes)::value_type));
		pub_keys.push_back (block->hashables.account.bytes.data ());
		signatures.push_back (block->signature.bytes.data ());
	}
	nano::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data () };
	checker.verify (check);
	bool all_valid = std::all_of (verifications.cbegin (), verifications.cend (), [] (auto verification) { return verification == 1; });
	ASSERT_TRUE (all_valid);
}

TEST (signature_checker, many_multi_threaded)
{
	nano::signature_checker checker (4);

	auto signature_checker_work_func = [&checker] () {
		nano::keypair key;
		nano::block_builder builder;
		auto block = builder
					 .state ()
					 .account (key.pub)
					 .previous (0)
					 .representative (key.pub)
					 .balance (0)
					 .link (0)
					 .sign (key.prv, key.pub)
					 .work (0)
					 .build ();
		auto block_hash = block->hash ();

		auto invalid_block = builder
							 .state ()
							 .account (key.pub)
							 .previous (0)
							 .representative (key.pub)
							 .balance (0)
							 .link (0)
							 .sign (key.prv, key.pub)
							 .work (0)
							 .build ();
		invalid_block->signature.bytes[31] ^= 0x1;
		auto invalid_block_hash = block->hash ();

		constexpr auto num_check_sizes = 18;
		constexpr std::array<size_t, num_check_sizes> check_sizes{ 2048, 256, 1024, 1,
			4096, 512, 2050, 1024, 8092, 513, 17, 1024, 2047, 255, 513, 2049, 1025, 1023 };

		std::vector<nano::signature_check_set> signature_checker_sets;
		signature_checker_sets.reserve (num_check_sizes);

		// Create containers so everything is kept in scope while the threads work on the signature checks
		std::array<std::vector<unsigned char const *>, num_check_sizes> messages;
		std::array<std::vector<size_t>, num_check_sizes> lengths;
		std::array<std::vector<unsigned char const *>, num_check_sizes> pub_keys;
		std::array<std::vector<unsigned char const *>, num_check_sizes> signatures;
		std::array<std::vector<int>, num_check_sizes> verifications;

		// Populate all the signature check sets. The last one in each set is given an incorrect block signature.
		for (int i = 0; i < num_check_sizes; ++i)
		{
			auto check_size = check_sizes[i];
			ASSERT_GT (check_size, 0);
			auto last_signature_index = check_size - 1;

			messages[i].resize (check_size);
			std::fill (messages[i].begin (), messages[i].end (), block_hash.bytes.data ());
			messages[i][last_signature_index] = invalid_block_hash.bytes.data ();

			lengths[i].resize (check_size);
			std::fill (lengths[i].begin (), lengths[i].end (), sizeof (decltype (block_hash)));

			pub_keys[i].resize (check_size);
			std::fill (pub_keys[i].begin (), pub_keys[i].end (), block->hashables.account.bytes.data ());
			pub_keys[i][last_signature_index] = invalid_block->hashables.account.bytes.data ();

			signatures[i].resize (check_size);
			std::fill (signatures[i].begin (), signatures[i].end (), block->signature.bytes.data ());
			signatures[i][last_signature_index] = invalid_block->signature.bytes.data ();

			verifications[i].resize (check_size);

			signature_checker_sets.emplace_back (check_size, messages[i].data (), lengths[i].data (), pub_keys[i].data (), signatures[i].data (), verifications[i].data ());
			checker.verify (signature_checker_sets[i]);

			// Confirm all but last are valid
			auto all_valid = std::all_of (verifications[i].cbegin (), verifications[i].cend () - 1, [] (auto verification) { return verification == 1; });
			ASSERT_TRUE (all_valid);
			ASSERT_EQ (verifications[i][last_signature_index], 0);
		}
	};

	std::thread signature_checker_thread1 (signature_checker_work_func);
	std::thread signature_checker_thread2 (signature_checker_work_func);

	signature_checker_thread1.join ();
	signature_checker_thread2.join ();
}

TEST (signature_checker, one)
{
	nano::signature_checker checker (0);

	auto verify_block = [&checker] (auto & block, auto result) {
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
			hashes.push_back (block->hash ());
			messages.push_back (hashes.back ().bytes.data ());
			lengths.push_back (sizeof (decltype (hashes)::value_type));
			pub_keys.push_back (block->hashables.account.bytes.data ());
			signatures.push_back (block->signature.bytes.data ());
		}
		nano::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data () };
		checker.verify (check);
		ASSERT_EQ (verifications.front (), result);
	};

	nano::keypair key;
	nano::block_builder builder;
	auto block = builder
				 .state ()
				 .account (key.pub)
				 .previous (0)
				 .representative (key.pub)
				 .balance (0)
				 .link (0)
				 .sign (key.prv, key.pub)
				 .work (0)
				 .build ();

	// Make signaure invalid and check result is incorrect
	block->signature.bytes[31] ^= 0x1;
	verify_block (block, 0);

	// Make it valid and check for succcess
	block->signature.bytes[31] ^= 0x1;
	verify_block (block, 1);
}

TEST (signature_checker, boundary_checks)
{
	// sizes container must be in incrementing order
	std::vector<size_t> sizes{ 0, 1 };
	auto add_boundary = [&sizes] (size_t boundary) {
		sizes.insert (sizes.end (), { boundary - 1, boundary, boundary + 1 });
	};

	for (auto i = 1; i <= 5; ++i)
	{
		add_boundary (nano::signature_checker::batch_size * i);
	}

	nano::signature_checker checker (1);
	auto max_size = *(sizes.end () - 1);
	std::vector<nano::uint256_union> hashes;
	hashes.reserve (max_size);
	std::vector<unsigned char const *> messages;
	messages.reserve (max_size);
	std::vector<size_t> lengths;
	lengths.reserve (max_size);
	std::vector<unsigned char const *> pub_keys;
	pub_keys.reserve (max_size);
	std::vector<unsigned char const *> signatures;
	signatures.reserve (max_size);
	nano::keypair key;
	nano::block_builder builder;
	auto block = builder
				 .state ()
				 .account (key.pub)
				 .previous (0)
				 .representative (key.pub)
				 .balance (0)
				 .link (0)
				 .sign (key.prv, key.pub)
				 .work (0)
				 .build ();

	size_t last_size = 0;
	for (auto size : sizes)
	{
		// The size needed to append to existing containers, saves re-initializing from scratch each iteration
		auto extra_size = size - last_size;

		std::vector<int> verifications;
		verifications.resize (size);
		for (auto i (0); i < extra_size; ++i)
		{
			hashes.push_back (block->hash ());
			messages.push_back (hashes.back ().bytes.data ());
			lengths.push_back (sizeof (decltype (hashes)::value_type));
			pub_keys.push_back (block->hashables.account.bytes.data ());
			signatures.push_back (block->signature.bytes.data ());
		}
		nano::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data () };
		checker.verify (check);
		bool all_valid = std::all_of (verifications.cbegin (), verifications.cend (), [] (auto verification) { return verification == 1; });
		ASSERT_TRUE (all_valid);
		last_size = size;
	}
}
