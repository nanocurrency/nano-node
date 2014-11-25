#include <gtest/gtest.h>
#include <rai/core/core.hpp>

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
    block_path.generate (hash1);
    ASSERT_EQ (2, path.size ());
    ASSERT_EQ (0, blocks.size ());
    ASSERT_EQ (*block1, *path [0]);
    ASSERT_EQ (*block2, *path [1]);
}