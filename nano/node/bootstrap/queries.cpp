#include <nano/messages/asc_pull.hpp>
#include <nano/node/bootstrap/queries.hpp>

namespace nano::bootstrap
{
nano::messages::asc_pull_req build_message (query_descriptor const & query, nano::network_constants const & network_constants, id_t id)
{
	struct builder
	{
		nano::network_constants const & network_constants;
		id_t id;

		nano::messages::asc_pull_req operator() (blocks_query const & query) const
		{
			nano::messages::asc_pull_req message{ network_constants };
			message.id = id;
			message.type = nano::messages::asc_pull_type::blocks;

			nano::messages::asc_pull_req::blocks_payload payload;
			payload.start = query.start;
			payload.count = query.count;
			payload.start_type = query.type == query_type::blocks_by_hash ? nano::messages::asc_pull_req::hash_type::block : nano::messages::asc_pull_req::hash_type::account;
			message.payload = payload;
			message.update_header ();
			return message;
		}

		nano::messages::asc_pull_req operator() (account_info_query const & query) const
		{
			nano::messages::asc_pull_req message{ network_constants };
			message.id = id;
			message.type = nano::messages::asc_pull_type::account_info;

			nano::messages::asc_pull_req::account_info_payload payload;
			payload.target = query.target;
			payload.target_type = nano::messages::asc_pull_req::hash_type::block; // Query account info by block hash
			message.payload = payload;
			message.update_header ();
			return message;
		}

		nano::messages::asc_pull_req operator() (frontiers_query const & query) const
		{
			nano::messages::asc_pull_req message{ network_constants };
			message.id = id;
			message.type = nano::messages::asc_pull_type::frontiers;

			nano::messages::asc_pull_req::frontiers_payload payload;
			payload.start = query.start;
			payload.count = query.count;
			message.payload = payload;
			message.update_header ();
			return message;
		}

		nano::messages::asc_pull_req operator() (topo_index_query const & query) const
		{
			nano::messages::asc_pull_req message{ network_constants };
			message.id = id;
			message.type = nano::messages::asc_pull_type::topo_index;

			nano::messages::asc_pull_req::topo_index_payload payload;
			payload.start = query.start;
			payload.count = static_cast<uint16_t> (query.count);
			message.payload = payload;
			message.update_header ();
			return message;
		}

		nano::messages::asc_pull_req operator() (blocks_random_query const & query) const
		{
			nano::messages::asc_pull_req message{ network_constants };
			message.id = id;
			message.type = nano::messages::asc_pull_type::blocks_random;

			nano::messages::asc_pull_req::blocks_random_payload payload;
			payload.hashes = query.hashes;
			message.payload = payload;
			message.update_header ();
			return message;
		}
	};
	return std::visit (builder{ network_constants, id }, query);
}

query_keys index_keys (query_descriptor const & query)
{
	struct extractor
	{
		query_keys operator() (blocks_query const & query) const
		{
			return { query.account, query.type == query_type::blocks_by_hash ? query.start.as_block_hash () : nano::block_hash{ 0 } };
		}
		query_keys operator() (account_info_query const & query) const
		{
			return { nano::account{ 0 }, query.target };
		}
		query_keys operator() (frontiers_query const & query) const
		{
			return { query.start, nano::block_hash{ 0 } };
		}
		query_keys operator() (topo_index_query const &) const
		{
			// Topo tags are tracked by id only; dedup is handled by the topo_blocks engine state
			return { nano::account{ 0 }, nano::block_hash{ 0 } };
		}
		query_keys operator() (blocks_random_query const &) const
		{
			// Random fetch carries many hashes; tracked by id only
			return { nano::account{ 0 }, nano::block_hash{ 0 } };
		}
	};
	return std::visit (extractor{}, query);
}

query_type to_query_type (query_descriptor const & query)
{
	struct visitor
	{
		query_type operator() (blocks_query const & query) const
		{
			return query.type;
		}
		query_type operator() (account_info_query const &) const
		{
			return query_type::account_info_by_hash;
		}
		query_type operator() (frontiers_query const &) const
		{
			return query_type::frontiers;
		}
		query_type operator() (topo_index_query const &) const
		{
			return query_type::topo_index;
		}
		query_type operator() (blocks_random_query const &) const
		{
			return query_type::blocks_random;
		}
	};
	return std::visit (visitor{}, query);
}
}
