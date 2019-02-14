#pragma once

#include <nano/node/stats.hpp>

namespace nano {
class message;
class message_sink
{
public:
	virtual ~message_sink () = default;
	void sink (nano::message const &) const;
	void send_buffer (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail) const;
	virtual void send_buffer_raw (uint8_t const *, size_t, std::function<void(boost::system::error_code const &, size_t)>) const = 0;
	virtual std::function<void(boost::system::error_code const &, size_t)> callback (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail) const = 0;
	virtual std::string to_string () const = 0;
};
}
