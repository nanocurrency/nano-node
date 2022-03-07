#include "lmdb/libraries/liblmdb/lmdb.h"
#include "nano/crypto_lib/random_pool.hpp"
#include "nano/lib/blocks.hpp"
#include "nano/lib/numbers.hpp"
#include "nano/node/lmdb/lmdb_env.hpp"
#include "nano/secure/utility.hpp"

#include <gtest/gtest.h>

#include <endian.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace
{
    using DataAndSizeContainer = std::pair<std::uint8_t*, std::size_t>;

    auto create_db()
    {
        bool failure{};
        auto env = std::make_unique<nano::mdb_env>(failure, nano::unique_path());

        return std::make_tuple(failure, std::move(env));
    }

    auto create_table(const nano::mdb_env& env, const nano::write_transaction& tx, const std::string_view& table_name)
    {
        MDB_dbi table{};
        const auto create_table_result = mdb_dbi_open(env.tx(tx), table_name.data(), MDB_CREATE, &table);

        return std::make_tuple(MDB_SUCCESS != create_table_result, table);
    }

    auto generate_random_data_to_use_as_key()
    {
        std::array<std::uint8_t, sizeof(nano::block_hash)> result{};
        nano::random_pool::generate_block(result.data(), result.size());

        return result;
    }

    auto generate_random_data_to_use_as_value()
    {
        std::array<std::uint8_t, nano::state_block::size> result{};
        nano::random_pool::generate_block(result.data(), result.size());

        return result;
    }

    template <typename ContainerT>
    DataAndSizeContainer get_data_and_size(const ContainerT& container)
    {
        if constexpr (std::is_same_v<ContainerT, DataAndSizeContainer>)
        {
            return container;
        }
        else
        {
            return std::make_pair(const_cast<std::uint8_t*>(container.data()), container.size());
        }
    }

    template <typename KeyT, typename ValueT>
    void perform_insertion(const nano::mdb_env& env,
                           nano::write_transaction& tx,
                           MDB_dbi table,
                           const KeyT& key,
                           const ValueT& value,
                           std::uint32_t flags)
    {
        MDB_val mdb_key{};
        const auto [key_data, key_size] = get_data_and_size(key);
        mdb_key.mv_data = key_data;
        mdb_key.mv_size = key_size;

        MDB_val mdb_value{};
        const auto [value_data, value_size] = get_data_and_size(value);
        mdb_value.mv_data = value_data;
        mdb_value.mv_size = value_size;

        const auto insert_result = mdb_put(env.tx(tx), table, &mdb_key, &mdb_value, flags);
        ASSERT_EQ (MDB_SUCCESS, insert_result);
    }

    void print_statistics(const nano::mdb_env& env,
                          nano::write_transaction& tx,
                          MDB_dbi table,
                          std::uint64_t itr,
                          const std::chrono::steady_clock::time_point& begin)
    {
        MDB_stat statistics{};
        const auto get_statistics_result = mdb_stat(env.tx (tx), table, &statistics);
        ASSERT_EQ (MDB_SUCCESS, get_statistics_result);
        tx.commit ();

        const auto now = std::chrono::steady_clock::now();
        const auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(now - begin);
        std::cout << "elapsed time = " << elapsedTime.count() << " seconds; "
                  << "insertion no. = " << std::to_string(itr) << "; "
                  << "B-Tree height = " << std::to_string(statistics.ms_depth) << "; "
                  << "non-leaf pages = " << std::to_string(statistics.ms_branch_pages) << "; "
                  << "leaf pages = " << std::to_string(statistics.ms_leaf_pages) << std::endl;

        tx.renew ();
    }
}

TEST(lmdb_performance, insert_normal)
{
    auto [db_creation_failed, env] = create_db();
    ASSERT_FALSE(db_creation_failed);

    auto tx = env->tx_begin_write();
    auto [table_creation_failed, table] = create_table(*env, tx, "test_table");
    ASSERT_FALSE(table_creation_failed);

    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t itr = 0; itr != 25000000; ++itr)
    {
        const auto key = generate_random_data_to_use_as_key();
        const auto value = generate_random_data_to_use_as_value();
        perform_insertion(*env, tx, table, key, value, 0);

        if (itr && 0 == itr % 1000000)
        {
            print_statistics(*env, tx, table, itr, begin);
        }
    }
}

TEST(lmdb_performance, insert_via_off_table)
{
    auto [db_creation_failed, env] = create_db();
    ASSERT_FALSE(db_creation_failed);

    auto tx = env->tx_begin_write();
    auto [main_table_creation_failed, main_table] = create_table(*env, tx, "main_table");
    ASSERT_FALSE(main_table_creation_failed);

    auto [off_table_creation_failed, off_table] = create_table(*env, tx, "off_table");
    ASSERT_FALSE(off_table_creation_failed);

    const auto begin = std::chrono::steady_clock::now();
    for (std::uint64_t itr = 0; itr != 25000000; ++itr)
    {
        std::uint64_t bigEndianItr = htobe64(itr);
        const DataAndSizeContainer main_table_key{reinterpret_cast<std::uint8_t*>(&bigEndianItr), sizeof(bigEndianItr)};
        const auto main_table_value = generate_random_data_to_use_as_value();
        perform_insertion(*env,
                          tx,
                          main_table,
                          main_table_key,
                          main_table_value,
                          MDB_APPEND);

        if (itr && 0 == itr % 1000000)
        {
            print_statistics(*env, tx, main_table, itr, begin);
        }

        const auto off_table_key = generate_random_data_to_use_as_key();
        perform_insertion(*env, tx, off_table, off_table_key, main_table_key, 0);
    }
}

GTEST_API_ int main(int argc, char** argv)
{
    std::cout << "Executing lmdb_performance tests" << std::endl;

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
