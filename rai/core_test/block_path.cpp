#include <gtest/gtest.h>
#include <rai/core/core.hpp>

TEST (block_path, zero)
{
	std::vector <std::unique_ptr <rai::block>> path;
	std::unordered_map <rai::block_hash, std::unique_ptr <rai::block>> blocks;
	rai::block_path block_path (path, blocks);
	block_path.generate (0);
	ASSERT_EQ (0, path.size ());
	ASSERT_EQ (0, blocks.size ());
}

TEST (block_path, one)
{
    std::vector <std::unique_ptr <rai::block>> path;
    std::unordered_map <rai::block_hash, std::unique_ptr <rai::block>> blocks;
    std::unique_ptr <rai::block> block1 (new rai::send_block);
    auto hash1 (block1->hash ());
    blocks [hash1] = block1->clone ();
    rai::block_path block_path (path, blocks);
    block_path.generate (hash1);
    ASSERT_EQ (1, path.size ());
    ASSERT_EQ (0, blocks.size ());
    ASSERT_EQ (*block1, *path [0]);
}

TEST (block_path, two)
{
    std::vector <std::unique_ptr <rai::block>> path;
    std::unordered_map <rai::block_hash, std::unique_ptr <rai::block>> blocks;
    std::unique_ptr <rai::send_block> block1 (new rai::send_block);
    auto hash1 (block1->hash ());
    blocks [hash1] = block1->clone ();
    std::unique_ptr <rai::send_block> block2 (new rai::send_block);
    block2->hashables.previous = hash1;
    auto hash2 (block2->hash ());
    blocks [hash2] = block2->clone ();
    rai::block_path block_path (path, blocks);
    block_path.generate (hash2);
    ASSERT_EQ (2, path.size ());
    ASSERT_EQ (0, blocks.size ());
    ASSERT_EQ (*block2, *path [0]);
    ASSERT_EQ (*block1, *path [1]);
}

TEST (block_path, receive_one)
{
	std::vector <std::unique_ptr <rai::block>> path;
	std::unordered_map <rai::block_hash, std::unique_ptr <rai::block>> blocks;
	std::unique_ptr <rai::send_block> block1 (new rai::send_block);
	block1->hashables.previous = 1;
	auto hash1 (block1->hash ());
	blocks [hash1] = block1->clone ();
	std::unique_ptr <rai::send_block> block2 (new rai::send_block);
	block2->hashables.previous = 2;
	auto hash2 (block2->hash ());
	blocks [hash2] = block2->clone ();
	std::unique_ptr <rai::receive_block> block3 (new rai::receive_block);
	block3->hashables.previous = hash1;
	block3->hashables.source = hash2;
	auto hash3 (block3->hash ());
	blocks [hash3] = block3->clone ();
	rai::block_path block_path (path, blocks);
	block_path.generate (hash3);
	ASSERT_EQ (3, path.size ());
	ASSERT_EQ (0, blocks.size ());
	ASSERT_EQ (*block3, *path [0]);
	ASSERT_EQ (*block2, *path [1]);
	ASSERT_EQ (*block1, *path [2]);
}

TEST (block_path, receive_two)
{
	std::vector <std::unique_ptr <rai::block>> path;
	std::unordered_map <rai::block_hash, std::unique_ptr <rai::block>> blocks;
	std::unique_ptr <rai::send_block> block1 (new rai::send_block);
	block1->hashables.previous = 1;
	auto hash1 (block1->hash ());
	blocks [hash1] = block1->clone ();
	std::unique_ptr <rai::send_block> block4 (new rai::send_block);
	auto hash4 (block4->hash ());
	blocks [hash4] = block4->clone ();
	std::unique_ptr <rai::send_block> block2 (new rai::send_block);
	block2->hashables.previous = hash4;
	auto hash2 (block2->hash ());
	blocks [hash2] = block2->clone ();
	std::unique_ptr <rai::receive_block> block3 (new rai::receive_block);
	block3->hashables.previous = hash1;
	block3->hashables.source = hash2;
	auto hash3 (block3->hash ());
	blocks [hash3] = block3->clone ();
	rai::block_path block_path (path, blocks);
	block_path.generate (hash3);
	ASSERT_EQ (4, path.size ());
	ASSERT_EQ (0, blocks.size ());
	ASSERT_EQ (*block3, *path [0]);
	ASSERT_EQ (*block2, *path [1]);
	ASSERT_EQ (*block4, *path [2]);
	ASSERT_EQ (*block1, *path [3]);
}