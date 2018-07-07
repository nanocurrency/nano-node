#include <rai/lib/interface.h>
#include <rai/node/api.hpp>
#include <rai/node/node.hpp>

using RequestType = nano::api::RequestType;

/** RPC specific errors messages */
std::string nano::error_api_messages::message (int ev) const
{
	switch (static_cast<nano::error_api> (ev))
	{
		case nano::error_api::generic:
			return "Unknown error";
		case nano::error_api::bad_threshold_number:
			return "Bad threshold number";
		case nano::error_api::control_disabled:
			return "Control is disabled";
		case nano::error_api::invalid_count_limit:
			return "Invalid count limit";
		case nano::error_api::invalid_offset:
			return "Invalid offset";
		case nano::error_api::invalid_sources_number:
			return "Invalid sources number";
		case nano::error_api::invalid_starting_account:
			return "Invalid starting account";
		case nano::error_api::invalid_destinations_number:
			return "Invalid destinations number";
		case nano::error_api::unsupport_message:
			return "Unsupported message";
	}

	return "Invalid error error";
}

nano::api::api_handler::api_handler (rai::node & node_a) :
node (node_a)
{
}

auto nano::api::api_handler::request (nano::api::req_account_pending request_a) -> maybe_unique_ptr<nano::api::res_account_pending>
{
	std::error_code ec;
	auto res = std::make_unique<nano::api::res_account_pending> ();

	rai::uint128_union threshold (0);
	if (request_a.has_threshold ())
	{
		if (threshold.decode_dec (request_a.threshold ().value ()))
		{
			ec = error_api::bad_threshold_number;
		}
	}

	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		for (auto & account_text : request_a.accounts ())
		{
			rai::uint256_union account;
			if (!account.decode_account (account_text))
			{
				auto pending_account = res->add_pending ();
				pending_account->set_account (account_text);
				rai::account end (account.number () + 1);
				for (auto i (node.store.pending_begin (transaction, rai::pending_key (account, 0))), n (node.store.pending_begin (transaction, rai::pending_key (end, 0))); i != n && pending_account->block_info_size () < request_a.count (); ++i)
				{
					rai::pending_info info (i->second);
					if (info.amount.number () >= threshold.number ())
					{
						rai::pending_key key (i->first);
						auto block_info = pending_account->add_block_info ();
						block_info->set_hash (key.hash.to_string ());
						if (request_a.source ())
						{
							block_info->set_amount (info.amount.number ().convert_to<std::string> ());
							block_info->set_source (info.source.to_account ());
						}
					}
				}
			}
			else
			{
				ec = nano::error_common::bad_account_number;
				break;
			}
		}
	}

	return either (res, ec);
}

auto nano::api::api_handler::request (nano::api::req_ping request_a) -> maybe_unique_ptr<nano::api::res_ping>
{
	auto res = std::make_unique<nano::api::res_ping> ();
	res->set_id (request_a.id ());
	return std::move (res);
}

auto nano::api::api_handler::request (req_address_valid request_a) -> maybe_unique_ptr<nano::api::res_address_valid>
{
	auto res = std::make_unique<nano::api::res_address_valid> ();
	res->set_valid (xrb_valid_address (request_a.address ().c_str ()) == 0);
	return std::move (res);
}

template <typename REQUST_TYPE>
decltype (auto) nano::api::api_handler::parse_and_request (std::vector<uint8_t> buffer_a)
{
	REQUST_TYPE req_obj;
	req_obj.ParseFromArray (buffer_a.data (), buffer_a.size ());
	return request (req_obj);
}

auto nano::api::api_handler::parse (nano::api::RequestType request_type_a, std::vector<uint8_t> buffer_a) -> maybe_unique_ptr<google::protobuf::Message>
{
	maybe_unique_ptr<google::protobuf::Message> msg;
	switch (request_type_a)
	{
		case RequestType::PING:
		{
			msg = parse_and_request<req_ping> (buffer_a);
			break;
		}
		case RequestType::ACCOUNT_PENDING:
		{
			msg = parse_and_request<req_account_pending> (buffer_a);
			break;
		}
		case RequestType::ADDRESS_VALID:
		{
			msg = parse_and_request<req_address_valid> (buffer_a);
			break;
		}
		default:
			return make_unexpected (nano::error_api::unsupport_message);
	};

	return msg;
}
