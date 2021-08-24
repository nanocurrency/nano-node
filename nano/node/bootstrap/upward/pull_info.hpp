#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace nano
{

class public_key;
using account = public_key;
class account_info;
class block;

namespace bootstrap
{

namespace upward
{

class pull_info final
{
public:
    pull_info (nano::account const & account_a,
               std::optional<nano::account_info> const & account_info_a,
               std::function<void()> error_callback_a,
               std::function<void(std::shared_ptr<nano::block>)> block_pulled_callback_a) :
    account{ account_a },
    account_info{ account_info_a },
    error_callback{ std::move (error_callback_a) },
    block_pulled_callback{ std::move (block_pulled_callback_a) }
    {

    }

    nano::account const & account;
    std::optional<nano::account_info> const & account_info;
    std::function<void()> error_callback;
    std::function<void(std::shared_ptr<nano::block>)> block_pulled_callback;
};

} // namespace upward

} // namespace bootstrap

} // namespace nano
