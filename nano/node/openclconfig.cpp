#include <nano/node/openclconfig.hpp>

nano::opencl_config::opencl_config (unsigned platform_a, unsigned device_a, unsigned threads_a) :
platform (platform_a),
device (device_a),
threads (threads_a)
{
}

nano::error nano::opencl_config::serialize_json (nano::jsonconfig & json) const
{
	json.put ("platform", platform);
	json.put ("device", device);
	json.put ("threads", threads);
	return json.get_error ();
}

nano::error nano::opencl_config::deserialize_json (nano::jsonconfig & json)
{
	json.get_optional<unsigned> ("platform", platform);
	json.get_optional<unsigned> ("device", device);
	json.get_optional<unsigned> ("threads", threads);
	return json.get_error ();
}
