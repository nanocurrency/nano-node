#pragma once

#include <nano/node/transport/fwd.hpp>
#include <nano/secure/fwd.hpp>
#include <nano/store/fwd.hpp>

// TODO: Move to lib/fwd.hpp
namespace nano
{
class block;
class container_info;
}

namespace nano
{
class active_elections;
class block_processor;
class confirming_set;
class election;
class ledger;
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

namespace nano::scheduler
{
class hinted;
class manual;
class optimistic;
class priority;
}