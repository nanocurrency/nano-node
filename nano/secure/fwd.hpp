#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/store/fwd.hpp>

namespace nano
{
class account_info;
class keypair;
class ledger;
class ledger_cache;
class ledger_constants;
class network_params;
class pending_info;
class pending_key;
class vote;

enum class block_status;
}

namespace nano::secure
{
class read_transaction;
class transaction;
class write_transaction;
}
