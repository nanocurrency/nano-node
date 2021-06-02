#include <nano/node/testing.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/node.hpp>
#include "nano/secure/ledger.hpp"

namespace nano
{
    void dfs(const std::shared_ptr<nano::block>& start_block)
    {
        nano::system system{};
        const auto node = std::make_shared<nano::node> (system.io_ctx, nano::get_available_port (), nano::working_path (), system.logging, system.work);
        if (node->init_error ())
        {
            std::cerr << "error initializing node\n";
            return;
        }

        std::vector<nano::block_hash> block_container{start_block->hash()};
        std::vector<nano::block_hash> checkpoint_container{};
        auto transaction = node->ledger.store.tx_begin_read ();

        const auto get_block_container_capacity = []()
        {
            // TODO: return a bigger and bigger capacity, but definitely not this huge :)
            //
            return std::numeric_limits<std::size_t>::max();
        };

        const auto insert_block_hash = [&](const auto& block_hash)
        {
            if (!block_hash.is_zero())
            {
                if (block_container.size() < get_block_container_capacity())
                {
                    block_container.emplace_back(block_hash);
                }
                else
                {
                    checkpoint_container.emplace_back(block_container.back());
                    block_container.clear();
                }
            }
        };

        while (!block_container.empty())
        {
            const auto current_block = node->ledger.store.block_get(transaction, block_container.back());
            block_container.pop_back();

            std::cout << "visiting block " << current_block->hash().to_string() << "\n";

            insert_block_hash(current_block->previous());
            if (current_block->sideband().details.is_receive)
            {
                insert_block_hash(current_block->source());
            }
        }
    }

    void test_dfs()
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
