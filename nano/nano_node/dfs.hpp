#include <nano/node/testing.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/ledger.hpp>

namespace nano
{
    void dfs(const std::shared_ptr<nano::node>& node, const nano::block_hash& start_block_hash, const std::function<bool(const std::shared_ptr<nano::block>&)>& client_callback)
    {
        std::stack<nano::block_hash> blocks_to_visit{};
        std::unordered_set<nano::block_hash> blocks_visited{start_block_hash};

        const auto insert_block = [&](const auto& block)
        {
            if (blocks_visited.emplace(block->hash()).second)
            {
                blocks_to_visit.emplace(block->hash());
            }
        };

        auto transaction = node->ledger.store.tx_begin_read ();
        const auto pop_block = [&]()
        {
            auto block = node->ledger.store.block_get(transaction, blocks_to_visit.top());
            blocks_to_visit.pop();

            return block;
        };

        blocks_to_visit.emplace(start_block_hash);
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

    void test_dfs_with_chosen_block(const std::shared_ptr<nano::node>& node, const std::string& block_hash)
    {
        std::cout << "testing dfs with block " << block_hash << "\n";

        nano::block_hash hash{};
        if (hash.decode_hex(block_hash))
        {
            std::cerr << "parse error block hash\n";
            return;
        }

        dfs(node, hash, [](const auto& block)
        {
            std::cout << "visiting block " << block->hash().to_string() << "\n";
            return true;
        });
    }

    void test_dfs_with_genesis_frontier(const std::shared_ptr<nano::node>& node)
    {
        std::cout << "testing dfs with genesis frontier\n";

        nano::account_info genesis_account_info{};
        nano::genesis genesis_block{};

        if (node->ledger.store.account_get(node->ledger.store.tx_begin_read (), genesis_block.open->account(), genesis_account_info))
        {
            std::cerr << "error fetching genesis account info\n";
            return;
        }

        const auto genesis_block_hash = genesis_block.hash();
        dfs(node, genesis_account_info.head, [&](const auto& block)
        {
            std::cout << "visiting block " << block->hash().to_string() << "\n";
            return block->hash() != genesis_block_hash;
        });
    }

    void discover_senders(const std::shared_ptr<nano::node>& node, const std::string& account_address)
    {
        std::cout << "discovering senders for account " << account_address << "\n";

        nano::account account{};
        if (account.decode_account(account_address))
        {
            std::cerr << "error parsing account\n";
            return;
        }

        nano::account_info account_info{};
        if (node->ledger.store.account_get(node->ledger.store.tx_begin_read (), account, account_info))
        {
            std::cerr << "error fetching account info\n";
            return;
        }

        std::unordered_set<nano::account> senders{};
        dfs(node, account_info.head, [&](const auto& block)
        {
            if (block->sideband().details.is_receive)
            {
                std::cout << "received from " << block->account().to_string() << "\n";
                senders.emplace(block->account());
            }

            return true;
        });

        std::cout << "account " << account_address << " received funds from " << senders.size() << " other accounts\n";
    }

    void test_dfs()
    {
        nano::system system{};
        const auto node = std::make_shared<nano::node> (system.io_ctx, nano::get_available_port (), nano::working_path (), system.logging, system.work);
        if (node->init_error ())
        {
            std::cerr << "error initializing node\n";
            return;
        }

        test_dfs_with_genesis_frontier(node);
        // test_dfs_with_chosen_block(node, "6F98CEC4FAEED12C5DF38C4BE6A249C317D10089BDECE7F95036A8DEF50433AC");
        // discover_senders(node, "nano_1qato4k7z3spc8gq1zyd8xeqfbzsoxwo36a45ozbrxcatut7up8ohyardu1z");
    }
}
