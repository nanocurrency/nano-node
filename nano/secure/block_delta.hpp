#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/secure/account_info.hpp>
#include <nano/secure/pending_info.hpp>

#include <array>
#include <memory>
#include <optional>
#include <utility>

namespace nano
{
class block_delta
{
public:
	std::shared_ptr<nano::block> block;
	nano::account_info head;
	std::pair<std::optional<nano::pending_key>, std::optional<nano::pending_info>> receivable;
	std::pair<std::optional<nano::account>, std::optional<nano::amount>> weight;
};
}
