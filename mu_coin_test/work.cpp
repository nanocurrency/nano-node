#include <gtest/gtest.h>

#include <mu_coin/mu_coin.hpp>

extern "C"
{
#include <blake2/blake2.h>
}

TEST (blake2, simple)
{
    mu_coin::uint256_union input;
    mu_coin::uint256_union output;
    auto result (blake2 (output.bytes.data (), input.bytes.data (), nullptr, output.bytes.size (), input.bytes.size (), 0));
    ASSERT_EQ (0, result);
}

TEST (work, simple)
{
    mu_coin::work work (2);
    auto output (work.perform (0, 1));
    ASSERT_FALSE (output.is_zero ());
}

TEST (work, small)
{
    mu_coin::work work (16);
    auto output (work.perform (0, 32));
    ASSERT_FALSE (output.is_zero ());
}

TEST (work, full_verify)
{
    mu_coin::work work (32 * 1024);
    auto begin (std::chrono::high_resolution_clock::now ());
    work.perform (0, 32 * 1024);
    auto end (std::chrono::high_resolution_clock::now ());
    auto us (std::chrono::duration_cast <std::chrono::microseconds> (end - begin));
    std::cout << boost::str (boost::format ("Microseconds: %1%\n") % us.count ());
}

TEST (work, DISABLED_full_generate)
{
    mu_coin::work work (1024);
    mu_coin::uint256_union value;
    value.clear ();
    auto begin (std::chrono::high_resolution_clock::now ());
    for (auto i (0); i < 1024; ++i)
    {
        value = work.perform (0, 1024);
    }
    auto end (std::chrono::high_resolution_clock::now ());
    auto us (std::chrono::duration_cast <std::chrono::microseconds> (end - begin));
    std::cout << boost::str (boost::format ("Microseconds: %1%\n") % us.count ());
}