#pragma once

#include <nano/node/transport/channel.hpp>

#include <concepts>
#include <memory>

#include <fmt/format.h>

template <>
struct fmt::formatter<nano::transport::channel> : fmt::formatter<std::string>
{
	auto format (nano::transport::channel const & channel, fmt::format_context & ctx) const
	{
		return fmt::formatter<std::string>::format (channel.to_string (), ctx);
	}
};

template <typename T>
	requires std::derived_from<T, nano::transport::channel>
struct fmt::formatter<std::shared_ptr<T>> : fmt::formatter<std::string>
{
	auto format (std::shared_ptr<T> const & channel, fmt::format_context & ctx) const
	{
		if (channel)
		{
			return fmt::formatter<std::string>::format (channel->to_string (), ctx);
		}
		else
		{
			return fmt::formatter<std::string>::format ("<null>", ctx);
		}
	}
};
