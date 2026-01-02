#pragma once

namespace nano::store
{
enum class table;

class backend;
class ledger_store;
class read_transaction;
class transaction;
class write_transaction;
}

namespace nano::store::ledger
{
class account_view;
class block_view;
class confirmation_height_view;
class final_vote_view;
class online_weight_view;
class peer_view;
class pending_view;
class pruned_view;
class rep_weight_view;
class version_view;
}