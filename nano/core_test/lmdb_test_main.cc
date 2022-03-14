#include "lmdb/libraries/liblmdb/lmdb.h"
#include "nano/crypto_lib/random_pool.hpp"
#include "nano/lib/blocks.hpp"
#include "nano/lib/numbers.hpp"
#include "nano/node/lmdb/lmdb_env.hpp"
#include <nano/node/lmdb/lmdb_iterator.hpp>
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
    using MainKey = std::array<std::uint8_t, sizeof(nano::block_hash)>;
    using MainValue = std::array<std::uint8_t, nano::state_block::size>;

    constexpr std::size_t ITERATIONS_COUNT = 25000000;
    constexpr std::size_t PRINT_STATISTICS_THRESHOLD = 1000000;
    constexpr std::string_view MAIN_TABLE_NAME = "main_table";
    constexpr std::string_view OFF_TABLE_NAME = "off_table";

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

    auto open_table(const nano::mdb_env& env, const nano::transaction& tx, const std::string_view& table_name)
    {
        MDB_dbi table{};
        const auto open_table_result = mdb_dbi_open(env.tx(tx), table_name.data(), 0, &table);

        return std::make_tuple(MDB_SUCCESS != open_table_result, table);
    }

    MainKey generate_random_data_to_use_as_main_key()
    {
        MainKey result{};
        nano::random_pool::generate_block(result.data(), result.size());

        return result;
    }

    MainValue generate_random_data_to_use_as_main_value()
    {
        MainValue result{};
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
                           const nano::write_transaction& tx,
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
    auto [create_db_failed, env] = create_db();
    ASSERT_FALSE(create_db_failed);

    std::vector<MainKey> keys(ITERATIONS_COUNT);
    std::cout << "BEGIN WRITES \n\n";
    const auto begin = std::chrono::steady_clock::now();

    {
        auto tx = env->tx_begin_write();
        const auto [create_table_failed, table] = create_table(*env, tx, MAIN_TABLE_NAME);
        ASSERT_FALSE(create_table_failed);

        for (std::size_t itr = 0; itr <= ITERATIONS_COUNT; ++itr)
        {
            auto key = generate_random_data_to_use_as_main_key();
            const auto value = generate_random_data_to_use_as_main_value();
            perform_insertion(*env, tx, table, key, value, 0);
            keys[itr] = std::move(key);

            if (itr && 0 == itr % PRINT_STATISTICS_THRESHOLD)
            {
                print_statistics(*env, tx, table, itr, begin);
            }
        }
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(now - begin);
    std::cout << "FINISHED WRITES, time = " << elapsedTime.count() << " seconds \n\n";
    std::cout << "BEGIN READS \n\n";

    {
        const auto tx = env->tx_begin_read();
        const auto [open_table_failed, table] = open_table(*env, tx, MAIN_TABLE_NAME);
        ASSERT_FALSE(open_table_failed);

        for (const auto& key : keys)
        {
            MDB_val mdb_key{key.size(), const_cast<std::uint8_t*>(key.data())};
            MDB_val mdb_value{};

            const auto read_failed = mdb_get(env->tx(tx), table, &mdb_key, &mdb_value);
            ASSERT_EQ(MDB_SUCCESS, read_failed);
            ASSERT_EQ(MainValue{}.size(), mdb_value.mv_size);
        }
    }

    now = std::chrono::steady_clock::now();
    elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(now - begin);
    std::cout << "FINISHED READS, time = " << elapsedTime.count() << " seconds \n\n";

    auto tx = env->tx_begin_write();
    const auto [open_table_failed, table] = open_table(*env, tx, MAIN_TABLE_NAME);
    ASSERT_FALSE(open_table_failed);

    const auto delete_table_failed = mdb_drop(env->tx(tx), table, 1);
    ASSERT_EQ(MDB_SUCCESS, delete_table_failed);
}

TEST(lmdb_performance, insert_via_off_table)
{
    auto [create_db_failed, env] = create_db();
    ASSERT_FALSE(create_db_failed);

    std::vector<MainKey> keys(ITERATIONS_COUNT);
    std::cout << "BEGIN WRITES \n\n";
    const auto begin = std::chrono::steady_clock::now();

    {
        auto tx = env->tx_begin_write();
        const auto [create_main_table_failed, main_table] = create_table(*env, tx, MAIN_TABLE_NAME);
        ASSERT_FALSE(create_main_table_failed);

        const auto [create_off_table_failed, off_table] = create_table(*env, tx, OFF_TABLE_NAME);
        ASSERT_FALSE(create_off_table_failed);

        for (std::uint64_t itr = 0; itr <= ITERATIONS_COUNT; ++itr)
        {
            std::uint64_t bigEndianItr = htobe64(itr);
            const DataAndSizeContainer main_key{reinterpret_cast<std::uint8_t*>(&bigEndianItr), sizeof(bigEndianItr)};
            const auto main_value = generate_random_data_to_use_as_main_value();
            perform_insertion(*env,
                              tx,
                              main_table,
                              main_key,
                              main_value,
                              MDB_APPEND);

            if (itr && 0 == itr % PRINT_STATISTICS_THRESHOLD)
            {
                print_statistics(*env, tx, main_table, itr, begin);
            }

            auto off_key = generate_random_data_to_use_as_main_key();
            perform_insertion(*env, tx, off_table, off_key, main_key, 0);
            keys[itr] = std::move(off_key);
        }
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(now - begin);
    std::cout << "FINISHED WRITES, time = " << elapsedTime.count() << " seconds \n\n";
    std::cout << "BEGIN READS \n\n";

    {
        const auto tx = env->tx_begin_read();
        const auto [open_main_table_failed, main_table] = open_table(*env, tx, MAIN_TABLE_NAME);
        ASSERT_FALSE(open_main_table_failed);

        const auto [open_off_table_failed, off_table] = open_table(*env, tx, OFF_TABLE_NAME);
        ASSERT_FALSE(open_off_table_failed);

        for (const auto& key : keys)
        {
            MDB_val mdb_off_key{key.size(), const_cast<std::uint8_t*>(key.data())};
            MDB_val mdb_off_value{};

            auto read_failed = mdb_get(env->tx(tx), off_table, &mdb_off_key, &mdb_off_value);
            ASSERT_EQ(MDB_SUCCESS, read_failed);
            ASSERT_EQ(sizeof(std::uint64_t), mdb_off_value.mv_size);

            MDB_val mdb_main_value{};
            read_failed = mdb_get(env->tx(tx), main_table, &mdb_off_value, &mdb_main_value);
            ASSERT_EQ(MDB_SUCCESS, read_failed);
            ASSERT_EQ(MainValue{}.size(), mdb_main_value.mv_size);
        }
    }

    now = std::chrono::steady_clock::now();
    elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(now - begin);
    std::cout << "FINISHED READS, time = " << elapsedTime.count() << " seconds \n\n";

    auto tx = env->tx_begin_write();
    const auto [open_main_table_failed, main_table] = open_table(*env, tx, MAIN_TABLE_NAME);
    ASSERT_FALSE(open_main_table_failed);

    const auto delete_main_table_failed = mdb_drop(env->tx(tx), main_table, 1);
    ASSERT_EQ(MDB_SUCCESS, delete_main_table_failed);

    const auto [open_off_table_failed, off_table] = open_table(*env, tx, OFF_TABLE_NAME);
    ASSERT_FALSE(open_off_table_failed);

    const auto delete_off_table_failed = mdb_drop(env->tx(tx), off_table, 1);
    ASSERT_EQ(MDB_SUCCESS, delete_off_table_failed);
}

GTEST_API_ int main(int argc, char** argv)
{
    std::cout << "Executing lmdb_performance tests" << std::endl;

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
