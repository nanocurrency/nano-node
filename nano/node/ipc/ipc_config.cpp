#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/ipc/ipc_config.hpp>

nano::error nano::ipc::ipc_config::serialize_toml (nano::tomlconfig & toml) const
{
	nano::tomlconfig tcp_l;
	tcp_l.put ("enable", transport_tcp.enabled, "Enable or disable IPC via TCP server.\ntype:bool");
	tcp_l.put ("port", transport_tcp.port, "Server listening port.\ntype:uint16");
	tcp_l.put ("io_timeout", transport_tcp.io_timeout, "Timeout for requests.\ntype:seconds");
	// Only write out experimental config values if they're previously set explicitly in the config file
	if (transport_tcp.io_threads >= 0)
	{
		tcp_l.put ("io_threads", transport_tcp.io_threads, "Number of threads dedicated to TCP I/O. Experimental.\ntype:uint64_t");
	}
	toml.put_child ("tcp", tcp_l);

	nano::tomlconfig domain_l;
	if (transport_domain.io_threads >= 0)
	{
		domain_l.put ("io_threads", transport_domain.io_threads);
	}
	domain_l.put ("enable", transport_domain.enabled, "Enable or disable IPC via local domain socket.\ntype:bool");
	domain_l.put ("allow_unsafe", transport_domain.allow_unsafe, "If enabled, certain unsafe RPCs can be used. Not recommended for production systems.\ntype:bool");
	domain_l.put ("path", transport_domain.path, "Path to the local domain socket.\ntype:string");
	domain_l.put ("io_timeout", transport_domain.io_timeout, "Timeout for requests.\ntype:seconds");
	toml.put_child ("local", domain_l);

	nano::tomlconfig flatbuffers_l;
	flatbuffers_l.put ("skip_unexpected_fields_in_json", flatbuffers.skip_unexpected_fields_in_json, "Allow client to send unknown fields in json messages. These will be ignored.\ntype:bool");
	flatbuffers_l.put ("verify_buffers", flatbuffers.verify_buffers, "Verify that the buffer is valid before parsing. This is recommended when receiving data from untrusted sources.\ntype:bool");
	toml.put_child ("flatbuffers", flatbuffers_l);

	return toml.get_error ();
}

nano::error nano::ipc::ipc_config::deserialize_toml (nano::tomlconfig & toml)
{
	auto tcp_l (toml.get_optional_child ("tcp"));
	if (tcp_l)
	{
		tcp_l->get_optional<long> ("io_threads", transport_tcp.io_threads, -1);
		tcp_l->get<bool> ("allow_unsafe", transport_tcp.allow_unsafe);
		tcp_l->get<bool> ("enable", transport_tcp.enabled);
		tcp_l->get<uint16_t> ("port", transport_tcp.port);
		tcp_l->get<std::size_t> ("io_timeout", transport_tcp.io_timeout);
	}

	auto domain_l (toml.get_optional_child ("local"));
	if (domain_l)
	{
		domain_l->get_optional<long> ("io_threads", transport_domain.io_threads, -1);
		domain_l->get<bool> ("allow_unsafe", transport_domain.allow_unsafe);
		domain_l->get<bool> ("enable", transport_domain.enabled);
		domain_l->get<std::string> ("path", transport_domain.path);
		domain_l->get<std::size_t> ("io_timeout", transport_domain.io_timeout);
	}

	auto flatbuffers_l (toml.get_optional_child ("flatbuffers"));
	if (flatbuffers_l)
	{
		flatbuffers_l->get<bool> ("skip_unexpected_fields_in_json", flatbuffers.skip_unexpected_fields_in_json);
		flatbuffers_l->get<bool> ("verify_buffers", flatbuffers.verify_buffers);
	}

	return toml.get_error ();
}
