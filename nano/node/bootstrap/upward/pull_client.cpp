#include <nano/node/bootstrap/upward/pull_client.hpp>
#include <nano/node/bootstrap/upward/pull_info.hpp>
#include <nano/node/common.hpp>
#include <nano/node/transport/transport.hpp>

nano::bootstrap::upward::pull_client::pull_client (nano::transport::channel & connection_a,
                                                   nano::network_constants const & network_constants_a) :
connection{ connection_a },
network_constants{ network_constants_a }
{
	//
}

void nano::bootstrap::upward::pull_client::pull (nano::bootstrap::upward::pull_info const & pull_info_a)
{
    // build a bulk_pull_account_frontwards message with info from pull_info_a
    // this new message class needs to be designed; it should behave just like bulk_pull_account,
    // but with the semantics of frontwards pulling instead of backwards

    // nano::bulk_pull_account_frontwards message{ network_constants };

    nano::bulk_pull_account message { network_constants };
    connection.send (message,
                     [&pull_info_a] (boost::system::error_code const & error_code_a, std::size_t size_a)
                     {
                         if (error_code_a)
                         {
                             // definitely some args needed here because the callback should
                             // know how to continue pulling from a different peer

                             pull_info_a.error_callback ();
                         }
                         else
                         {
                             // download a block from the stream, parse it and call pull_info_a.block_pulled_callback
                         }
                     },
                     nano::buffer_drop_policy::no_limiter_drop);
}
