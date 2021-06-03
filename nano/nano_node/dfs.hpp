#include <nano/node/testing.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/ledger.hpp>

namespace nano
{
    void dfs(const nano::block_hash& start_block_hash, const std::function<bool(const std::shared_ptr<nano::block>&)>& client_callback)
    {
        nano::system system{};
        const auto node = std::make_shared<nano::node> (system.io_ctx, nano::get_available_port (), nano::working_path (), system.logging, system.work);
        if (node->init_error ())
        {
            std::cerr << "error initializing node\n";
            return;
        }

        std::vector<nano::block_hash> blocks_to_visit{start_block_hash};
        std::unordered_set<nano::block_hash> blocks_visited{start_block_hash};
        const auto insert_block = [&](const auto& block)
        {
            if (blocks_visited.emplace(block->hash()).second)
            {
                blocks_to_visit.emplace_back(block->hash());
            }
        };

        auto transaction = node->ledger.store.tx_begin_read ();
        const auto pop_block = [&]()
        {
            auto block = node->ledger.store.block_get(transaction, blocks_to_visit.back());
            blocks_to_visit.pop_back();

            return block;
        };

        while (!blocks_to_visit.empty())
        {
            const auto block = pop_block();
            if (!client_callback(block))
            {
                continue;
            }

            if (!block->previous().is_zero())
            {
                insert_block(node->ledger.store.block_get(transaction, block->previous()));
            }

            if (block->sideband().details.is_receive)
            {
                insert_block(node->ledger.store.block_get(transaction, block->link().as_block_hash()));
            }
        }
    }

    void test_dfs()
    {
        nano::block_hash hash{};
        if (hash.decode_hex("4EA5CE70091576BED73A637104E17194C45D0130D5812D99BCCB7B24104C613D"))
        {
            std::cerr << "parse error block hash\n";
            return;
        }

        const auto genesis_block_hash = nano::genesis{}.hash();
        dfs(hash, [&](const auto& block)
        {
            // std::cout << "visiting block " << block->hash().to_string() << "\n";
            return block->hash() != genesis_block_hash;
        });
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
