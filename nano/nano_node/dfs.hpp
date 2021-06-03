#include <nano/node/testing.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/node.hpp>
#include "nano/secure/ledger.hpp"

namespace nano
{
    struct account_walk_info
    {
        explicit account_walk_info(std::uint64_t from_height)
        : walked{false},
          from{from_height},
          to{0}
        {

        }

        void reset(std::uint64_t height)
        {
            debug_assert(walked);
            debug_assert(from < height);

            to = from;
            from = height;
            walked = false;
        }

        bool has_block_been_visited(std::uint64_t block_height) const
        {
            if (walked)
            {
                return block_height <= from;
            }

            debug_assert(to > 0);
            return block_height < to;
        }

        bool walked;
        std::uint64_t from;
        std::uint64_t to;
    };

    void dfs(const std::shared_ptr<nano::block>& start_block)
    {
        nano::system system{};
        const auto node = std::make_shared<nano::node> (system.io_ctx, nano::get_available_port (), nano::working_path (), system.logging, system.work);
        if (node->init_error ())
        {
            std::cerr << "error initializing node\n";
            return;
        }

        std::vector<nano::block_hash> block_hash_container{start_block->hash()};
        std::unordered_map<nano::account, account_walk_info> account_container{};
        const auto insert_block = [&](const auto& block)
        {
            const auto block_height = block->sideband().height;
            const auto [account_iterator, insertion_successful] = account_container.emplace(block->account(), account_walk_info{block_height});
            if (!insertion_successful)
            {
                auto& walk_info = account_iterator->second;
                if (!walk_info.has_block_been_visited(block_height))
                {
                    block_hash_container.emplace_back(block->hash());
                    if (walk_info.walked && walk_info.from < block_height)
                    {
                        walk_info.reset(block_height);
                    }
                }
            }
            else
            {
                block_hash_container.emplace_back(block->hash());
            }
        };

        auto transaction = node->ledger.store.tx_begin_read ();
        const auto pop_block_hash = [&]()
        {
            auto block = node->ledger.store.block_get(transaction, block_hash_container.back());
            block_hash_container.pop_back();

            return block;
        };

        const auto visit_block = [](const auto& block)
        {
            std::cout << "visiting block " << block->hash().to_string() << "\n";
        };

        while (!block_hash_container.empty())
        {
            const auto block = pop_block_hash();

            const auto account_iterator = account_container.find(block->account());
            if (account_iterator == account_container.cend())
            {
                continue;
            }

            const auto& walk_info = account_iterator->second;
            if (walk_info.has_block_been_visited(block->sideband().height))
            {
                continue;
            }

            visit_block(block);

            if (!block->previous().is_zero())
            {
                const auto previous_block = node->ledger.store.block_get(transaction, block->previous());
                if (previous_block->sideband().height >= account_iterator->second.to)
                {
                    insert_block(previous_block);
                }
                else
                {
                    account_iterator->second.walked = true;
                }
            }

            if (block->sideband().details.is_receive)
            {
                insert_block(node->ledger.store.block_get(transaction, block->source()));
            }
        }
    }

    void senders_discovery()
    {
        nano::system system{};
        const auto node = std::make_shared<nano::node> (system.io_ctx, nano::get_available_port (), nano::working_path (), system.logging, system.work);
        if (node->init_error ())
        {
            std::cerr << "error initializing node \n";
            return;
        }

        const auto trace_funds_from_account = [&node](const auto& account_name)
        {
            nano::account account{};
            if (account.decode_account (account_name))
            {
                std::cerr << "error parsing account\n";
                return;
            }

            std::unordered_map<nano::account, std::vector<nano::amount>> senders_and_amounts{};
            std::queue<nano::account> accounts_to_iterate{};
            std::unordered_set<nano::account> accounts_already_iterated{};
            accounts_to_iterate.push(account);
            accounts_already_iterated.insert(account);

            auto transaction = node->ledger.store.tx_begin_read ();
            while (!accounts_to_iterate.empty())
            {
                nano::account_info info{};
                if (node->ledger.store.account_get (transaction, accounts_to_iterate.front(), info))
                {
                    std::cerr << "error grabbing account info\n";
                    return;
                }

                accounts_to_iterate.pop();

                std::shared_ptr<nano::block> previous{};
                auto current = node->ledger.store.block_get(transaction, info.open_block);
                while (current)
                {
                    if (current->sideband().details.is_receive)
                    {
                        const auto source = node->ledger.store.block_get(transaction, current->source());
                        if (!source)
                        {
                            std::cerr << "error grabbing source\n";
                            return;
                        }

                        senders_and_amounts[source->account()].emplace_back(current->sideband().balance.number() - previous->sideband().balance.number());

                        if (accounts_already_iterated.insert(source->account()).second)
                        {
                            accounts_to_iterate.push(source->account());
                        }
                    }
                    else if (current->sideband().details.is_send)
                    {
                        // std::cout << "sent to " << current->link().as_account().to_account() << "\n";
                    }

                    previous = current;
                    current = node->ledger.store.block_get(transaction, current->sideband().successor);
                }
            }

            std::cout << "account " << account_name << " received funds from " << senders_and_amounts.size() << " other accounts\n";
        };

        trace_funds_from_account("nano_1qato4k7z3spc8gq1zyd8xeqfbzsoxwo36a45ozbrxcatut7up8ohyardu1z");
    }
}
