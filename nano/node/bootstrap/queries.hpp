#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/messages/fwd.hpp>
#include <nano/node/bootstrap/common.hpp>
#include <nano/secure/common.hpp>

#include <cstddef>
#include <deque>
#include <variant>

namespace nano::bootstrap
{
// Describes a blocks pull: which account chain, where to start, and how many blocks to fetch
struct blocks_query
{
	nano::account account{ 0 }; // Account chain being pulled (subject of the request)
	nano::hash_or_account start{ 0 }; // Pull starts from this block hash or account root
	size_t count{ 0 };
	query_type type{ query_type::blocks_by_account }; // Either blocks_by_hash or blocks_by_account
};

// Describes an account info query, used to discover the account that contains a given block
struct account_info_query
{
	nano::block_hash target{ 0 };
};

// Describes a frontiers scan query
struct frontiers_query
{
	nano::account start{ 0 };
	size_t count{ 0 };
};

// Describes a topology index scan query: walk the peer's topo index starting at `start`
struct topo_index_query
{
	nano::topo_key start{};
	size_t count{ 0 };
};

// Describes a random-blocks fetch: pull an arbitrary set of blocks by hash
struct blocks_random_query
{
	std::deque<nano::block_hash> hashes;
};

// A query descriptor carries everything needed to build the request message and to verify/process the response
using query_descriptor = std::variant<blocks_query, account_info_query, frontiers_query, topo_index_query, blocks_random_query>;

// Multi-index keys (account, hash) used to track an in-flight query
struct query_keys
{
	nano::account account{ 0 };
	nano::block_hash hash{ 0 };
};

// Builds the outgoing pull request message from a query descriptor
nano::messages::asc_pull_req build_message (query_descriptor const &, nano::network_constants const &, id_t id);

// Derives the multi-index tracking keys from a query descriptor
query_keys index_keys (query_descriptor const &);

// Derives the query type of a query descriptor
query_type to_query_type (query_descriptor const &);
}
