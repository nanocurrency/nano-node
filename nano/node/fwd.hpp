#pragma once

#include <nano/node/transport/fwd.hpp>
#include <nano/secure/fwd.hpp>
#include <nano/store/fwd.hpp>

namespace nano
{
class active_elections;
class block_processor;
class ledger;
class local_vote_history;
class logger;
class network;
class network_params;
class node;
class node_config;
class node_flags;
class node_observers;
class online_reps;
class rep_crawler;
class rep_tiers;
class stats;
class vote_cache;
class vote_generator;
class vote_processor;
class vote_router;
class wallets;

enum class block_source;
enum class vote_code;
}