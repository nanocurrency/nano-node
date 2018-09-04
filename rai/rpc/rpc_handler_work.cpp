#include <rai/lib/errors.hpp>
#include <rai/lib/interface.h>
#include <rai/node/node.hpp>
#include <rai/rpc/rpc.hpp>
#include <rai/rpc/rpc_handler.hpp>

uint64_t rai::rpc_handler::work_optional_impl ()
{
	uint64_t result (0);
	boost::optional<std::string> work_text (request.get_optional<std::string> ("work"));
	if (!ec && work_text.is_initialized ())
	{
		if (rai::from_string_hex (work_text.get (), result))
		{
			ec = nano::error_common::bad_work_format;
		}
	}
	return result;
}

void rai::rpc_handler::work_generate ()
{
	rpc_control_impl ();
	auto hash (hash_impl ());
	if (!ec)
	{
		bool use_peers (request.get_optional<bool> ("use_peers") == true);
		auto rpc_l (shared_from_this ());
		auto callback = [rpc_l](boost::optional<uint64_t> const & work_a) {
			if (work_a)
			{
				boost::property_tree::ptree response_l;
				response_l.put ("work", rai::to_string_hex (work_a.value ()));
				rpc_l->response (response_l);
			}
			else
			{
				error_response (rpc_l->response, "Cancelled");
			}
		};
		if (!use_peers)
		{
			node.work.generate (hash, callback);
		}
		else
		{
			node.work_generate (hash, callback);
		}
	}
	// Because of callback
	if (ec)
	{
		response_errors ();
	}
}

void rai::rpc_handler::work_cancel ()
{
	rpc_control_impl ();
	auto hash (hash_impl ());
	if (!ec)
	{
		node.work.cancel (hash);
	}
	response_errors ();
}

void rai::rpc_handler::work_get ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto account (account_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		if (wallet->store.find (transaction, account) != wallet->store.end ())
		{
			uint64_t work (0);
			auto error_work (wallet->store.work_get (transaction, account, work));
			response_l.put ("work", rai::to_string_hex (work));
		}
		else
		{
			ec = nano::error_common::account_not_found_wallet;
		}
	}
	response_errors ();
}

void rai::rpc_handler::work_set ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto account (account_impl ());
	auto work (work_optional_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_write ());
		if (wallet->store.find (transaction, account) != wallet->store.end ())
		{
			wallet->store.work_put (transaction, account, work);
			response_l.put ("success", "");
		}
		else
		{
			ec = nano::error_common::account_not_found_wallet;
		}
	}
	response_errors ();
}

void rai::rpc_handler::work_validate ()
{
	auto hash (hash_impl ());
	auto work (work_optional_impl ());
	if (!ec)
	{
		auto validate (rai::work_validate (hash, work));
		response_l.put ("valid", validate ? "0" : "1");
	}
	response_errors ();
}

void rai::rpc_handler::work_peer_add ()
{
	rpc_control_impl ();
	if (!ec)
	{
		std::string address_text = request.get<std::string> ("address");
		std::string port_text = request.get<std::string> ("port");
		uint16_t port;
		if (!rai::parse_port (port_text, port))
		{
			node.config.work_peers.push_back (std::make_pair (address_text, port));
			response_l.put ("success", "");
		}
		else
		{
			ec = nano::error_common::invalid_port;
		}
	}
	response_errors ();
}

void rai::rpc_handler::work_peers ()
{
	rpc_control_impl ();
	if (!ec)
	{
		boost::property_tree::ptree work_peers_l;
		for (auto i (node.config.work_peers.begin ()), n (node.config.work_peers.end ()); i != n; ++i)
		{
			boost::property_tree::ptree entry;
			entry.put ("", boost::str (boost::format ("%1%:%2%") % i->first % i->second));
			work_peers_l.push_back (std::make_pair ("", entry));
		}
		response_l.add_child ("work_peers", work_peers_l);
	}
	response_errors ();
}

void rai::rpc_handler::work_peers_clear ()
{
	rpc_control_impl ();
	if (!ec)
	{
		node.config.work_peers.clear ();
		response_l.put ("success", "");
	}
	response_errors ();
}
