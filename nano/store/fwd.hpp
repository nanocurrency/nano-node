#pragma once

namespace nano::store
{
enum class table;

class backend;
class meta_view;
class txn_tracking_config;
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
class receive_block_by_send_block_view;
class successor_view;
class rep_weight_view;
class topology_view;

using version_view = nano::store::meta_view;
}
