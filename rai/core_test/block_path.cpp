#include <gtest/gtest.h>
#include <rai/core/core.hpp>

TEST (block_path, construction)
{
    std::vector <std::unique_ptr <rai::block>> path;
    std::unordered_map <rai::block_hash, std::unique_ptr <rai::block>> blocks;
    rai::block_path block_path (path, blocks);
}