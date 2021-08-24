#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>

#include <deque>
#include <optional>
#include <unordered_set>

namespace nano
{

class account_info;
class block_processor;
class logger_mt;
class network_constants;
class store;
class thread_pool;

namespace bootstrap
{

namespace upward
{

class peer_manager;

class bootstrapper final
{
public:
    bootstrapper (nano::store const & store_a,
                  nano::block_processor & block_processor_a,
                  nano::bootstrap::upward::peer_manager & peer_manager_a,
                  nano::network_constants const & network_constants_a,
                  nano::logger_mt & logger_a,
                  nano::thread_pool & thread_pool_a);

    void start ();

    void pause ();

    void resume ();

    void stop ();

    bool is_running () const;

    void follow_account (nano::account const & account_a);

private:
    nano::store const & store;
    nano::block_processor & block_processor;
    nano::bootstrap::upward::peer_manager & peer_manager;
    nano::network_constants const & network_constants;
    nano::logger_mt & logger;
    nano::thread_pool & thread_pool;
    nano::mutex mutex;
    std::deque<nano::account> accounts_to_follow;
    std::unordered_set<nano::account> recently_followed_accounts;

    void boot ();

    void boot_impl ();

    void pull (nano::account const & account_a, std::optional<nano::account_info> const & account_info_a);
};

} // namespace upward

} // namespace bootstrap

} // namespace nano
