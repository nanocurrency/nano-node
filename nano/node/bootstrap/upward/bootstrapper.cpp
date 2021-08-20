#include <nano/lib/logger_mt.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/bootstrap/upward/bootstrapper.hpp>
#include <nano/node/bootstrap/upward/peer_manager.hpp>
#include <nano/node/bootstrap/upward/pull_client.hpp>
#include <nano/node/bootstrap/upward/pull_info.hpp>
#include <nano/secure/store.hpp>

#include <boost/format.hpp>

#include <utility>

nano::bootstrap::upward::bootstrapper::bootstrapper (nano::store const & store_a,
                                                     nano::bootstrap::upward::peer_manager & peer_manager_a,
                                                     nano::network_constants const & network_constants_a,
                                                     nano::logger_mt & logger_a,
                                                     nano::thread_pool & thread_pool_a) :
store{ store_a },
peer_manager { peer_manager_a },
network_constants{ network_constants_a },
logger{ logger_a },
thread_pool { thread_pool_a },
mutex{ }
{
	debug_assert (!store.init_error ());
}

void nano::bootstrap::upward::bootstrapper::start ()
{
    // if already started, issue an error

    boot (thread_pool);
}

void nano::bootstrap::upward::bootstrapper::pause ()
{
    // if already paused or stopped, issue an error
}

void nano::bootstrap::upward::bootstrapper::resume ()
{
    // if stopped or not paused, issue an error
}

void nano::bootstrap::upward::bootstrapper::stop ()
{
    // if already stopped, issue an error
}

bool nano::bootstrap::upward::bootstrapper::is_running () const
{
    // return true if both started and not paused

    return true;
}

void nano::bootstrap::upward::bootstrapper::follow_account (nano::account const & account_a)
{
    nano::lock_guard<nano::mutex> lock{ mutex };

	if (!recently_followed_accounts.count (account_a))
	{
		accounts_to_follow.push_front (account_a);
	}
}

void nano::bootstrap::upward::bootstrapper::boot (nano::thread_pool & thread_pool_a)
{
    // the "10min" value should be a combination of config value (default),
    // but also dynamic, depending on the size of accounts_to_follow, etc

    using namespace std::chrono_literals;
    thread_pool_a.add_timed_task (std::chrono::steady_clock::now () + 10min,
                                  [&thread_pool_a, this] ()
                                  {
                                      boot_impl ();
                                      boot (thread_pool_a);
                                  });
}

void nano::bootstrap::upward::bootstrapper::boot_impl ()
{
    // this method is the core of the bootstrapper; it iterates the accounts_to_follow container and
    // tries to download more blocks off the network for each of those accounts.
    //
    // accounts should make their way into the container by means of bootstrapper::follow_account.
    //
    // presumably, the (sole?) caller of follow_account should be an observer that is subscribed to the
    // block_has_been_confirmed notification (confirmation_height_processor?).
    //
    // the observer should check if the confirmed block was a SEND one,
    // and if it was then ask the bootstrapper to follow_account(confirmed_block->destination()).

    const auto transaction = store.tx_begin_read ();

    nano::lock_guard<nano::mutex> lock{ mutex };
    while (!accounts_to_follow.empty ())
    {
        const auto & account = accounts_to_follow.back ();
        recently_followed_accounts.emplace (account);

        nano::account_info account_info{};
        if (!store.account.get (transaction, account, account_info))
        {
            pull (account, account_info);
        }
        else
        {
            logger.always_log (boost::str (boost::format ("Unable to read account %1%") % account.to_account ()));
        }

        accounts_to_follow.pop_back ();
    }
}

void nano::bootstrap::upward::bootstrapper::pull (nano::account const & account_a,
                                                  nano::account_info const & account_info_a)
{
    const auto & peer = peer_manager.get_best ();

    auto error_callback = [] ()
    {
        // peer went offline, poor connection, or any other reason for which we
        // cannot rely on this peer, maybe we managed to pull something from him already,
        // but as of this moment we need to let the manager know and no longer rely on him

        // also, another thing that we need to take of here, is how do we continue?
        // should we just re-follow the account we were following but with a different peer?
    };

    auto block_pulled_callback = [&account_info_a] (auto block_a)
    {
        if (block_a->previous () != account_info_a.head)
        {
            // open block case needs to be treated as well, but otherwise, previous should == head.
            // in case they're not, our peer might be misbehaving, so definitely tell the manager about it
        }
        else
        {
            // block looks good at first sight, so put it through the processing pipeline;
            // eventually, if the block gets into unchecked_table, tell the manager to give this peer a raise
            // if the block gets confirmed later on, trust the peer even more.

            // good question: how do we "put the block through the processing pipeline"?

            // another question: any kind of basic/decent checks that we can make ourselves?
            //                   maybe signature, work, anything else?
        }
    };

	nano::bootstrap::upward::pull_client pull_client{ peer.get_connection (), network_constants };
	pull_client.pull (nano::bootstrap::upward::pull_info{ account_a,
                                                          account_info_a,
                                                          std::move (error_callback),
                                                          std::move (block_pulled_callback) });
}
