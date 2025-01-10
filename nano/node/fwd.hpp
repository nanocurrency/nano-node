#pragma once

#include <celerix/lib/fwd.hpp>
#include <celerix/node/transport/fwd.hpp>
#include <celerix/secure/fwd.hpp>
#include <celerix/store/fwd.hpp>

namespace celerix
{
class account_sets_config;
class active_elections;
class backlog_scan;
class block_processor;
class bounded_backlog;
class bucketing;
class bootstrap_config;
class bootstrap_server;
class bootstrap_service;
class confirming_set;
class election;
class election_status;
class local_block_broadcaster;
class local_vote_history;
class logger;
class network;
class network_params;
class node;
class node_config;
class node_flags;
class node_observers;
class online_reps;
class recently_cemented_cache;
class recently_confirmed_cache;
class rep_crawler;
class rep_tiers;
class telemetry;
class unchecked_map;
class stats;
class vote_cache;
enum class vote_code;
enum class vote_source;
class vote_generator;
class vote_processor;
class vote_router;
class vote_spacing;
class wallets;

enum class block_source;
enum class election_behavior;
enum class election_state;
enum class vote_code;
}

namespace celerix::scheduler
{
class component;
class hinted;
class manual;
class optimistic;
class priority;
}
