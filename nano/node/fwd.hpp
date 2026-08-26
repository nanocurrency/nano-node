#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/node/transport/fwd.hpp>
#include <nano/node/wallet/fwd.hpp>
#include <nano/secure/fwd.hpp>
#include <nano/store/fwd.hpp>

namespace nano
{
class account_sets_config;
class active_elections;
class active_elections_config;
class backlog_scan;
class backlog_scan_config;
class bandwidth_limiter;
class block_context;
class block_processor;
class block_processor_config;
class block_rebroadcaster;
class block_rebroadcaster_config;
class bounded_backlog;
class bounded_backlog_config;
class bucketing;
class bootstrap_config;
class bootstrap_server;
class bootstrap_server_config;
class bootstrap_service;
class cementing_set;
class cementing_set_config;
class distributed_work_factory;
class election;
class election_pacing;
struct election_status;
class epoch_upgrader;
class fork_cache;
class fork_cache_config;
class ledger_notifications;
class local_block_broadcaster;
class local_block_broadcaster_config;
class local_vote_history;
class logger;
class message_processor;
class message_processor_config;
class monitor;
class monitor_config;
class network;
class network_config;
class network_params;
class node;
class node_config;
class node_flags;
class node_observers;
class online_reps;
class peer_history;
class peer_history_config;
class port_mapping;
class pruning;
class recently_cemented_cache;
class recently_confirmed_cache;
class rep_crawler;
class rep_crawler_config;
class rep_tiers;
class http_callbacks;
class telemetry;
class unchecked_map;
class stats;
class vote_cache;
class vote_cache_config;
class vote_cache_processor;
class vote_generator;
class vote_generator_config;
class vote_replier;
class vote_replier_config;
class vote_processor;
class vote_processor_config;
class vote_rebroadcaster;
class vote_rebroadcaster_config;
class vote_router;
class vote_spacing;
class voting_policy;
class websocket_server;

enum class block_source;
enum class election_behavior;
enum class election_state;
enum class vote_code;
enum class vote_source;
}

namespace nano::bootstrap
{
class bootstrap_context;
}

namespace nano::ipc
{
class ipc_config;
}

namespace nano::scheduler
{
class component;
class hinted;
class hinted_config;
class manual;
class optimistic;
class optimistic_config;
class priority;
class priority_config;
}

namespace nano::websocket
{
class config;
}
