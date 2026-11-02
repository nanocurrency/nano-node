#pragma once

#include <nano/lib/enum_util.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/fwd.hpp>
#include <nano/node/transport/traffic_type.hpp>

#include <atomic>
#include <functional>
#include <future>

namespace nano::transport
{
class test_channel final : public nano::transport::channel
{
public:
	using channel::channel;

	/// Register a callback invoked for each sent message
	void observe (std::function<void (nano::messages::message const &, nano::transport::traffic_type)> observer)
	{
		observers.add (std::move (observer));
	}

	/// Returns a future that resolves when the next message of type MessageT is sent
	template <typename MessageT>
	std::future<MessageT> observe ()
	{
		auto promise = std::make_shared<std::promise<MessageT>> ();
		auto future = promise->get_future ();
		auto fulfilled = std::make_shared<std::atomic<bool>> (false);

		observers.add ([promise, fulfilled] (nano::messages::message const & message, nano::transport::traffic_type) {
			if (auto * casted = dynamic_cast<MessageT const *> (&message))
			{
				if (!fulfilled->exchange (true))
				{
					promise->set_value (*casted);
				}
			}
		});

		return future;
	}

	/// Register a callback invoked for each sent message of type MessageT
	template <typename MessageT>
	void observe (std::function<void (MessageT const &)> observer)
	{
		observers.add ([observer] (nano::messages::message const & message, nano::transport::traffic_type) {
			if (auto * casted = dynamic_cast<MessageT const *> (&message))
			{
				observer (*casted);
			}
		});
	}

	nano::endpoint get_remote_endpoint () const override
	{
		return {};
	}

	nano::endpoint get_local_endpoint () const override
	{
		return {};
	}

	nano::transport::transport_type get_type () const override
	{
		return nano::transport::transport_type::loopback;
	}

	void close () override
	{
		closed = true;
	}

	bool alive () const override
	{
		return !closed;
	}

	// Makes max () report the traffic type as full until unsaturate ()
	void saturate (nano::transport::traffic_type type)
	{
		saturated.at (type) = true;
	}

	void unsaturate (nano::transport::traffic_type type)
	{
		saturated.at (type) = false;
	}

	bool max (nano::transport::traffic_type type) override
	{
		return saturated.at (type);
	}

	std::string to_string () const override
	{
		return "test_channel";
	}

protected:
	bool send_impl (nano::messages::message const & message, nano::transport::traffic_type traffic_type, callback_t callback) override
	{
		observers.notify (message, traffic_type);

		if (callback)
		{
			callback (boost::system::errc::make_error_code (boost::system::errc::success), message.to_shared_const_buffer ().size ());
		}

		return true;
	}

	bool send_buffer_impl (nano::shared_const_buffer const & buffer, nano::transport::traffic_type traffic_type, callback_t callback) override
	{
		return send_buffer_as_message (buffer, traffic_type, std::move (callback));
	}

private:
	nano::observer_set<nano::messages::message, nano::transport::traffic_type> observers;
	nano::enum_array<nano::transport::traffic_type, std::atomic<bool>> saturated{};
	std::atomic<bool> closed{ false };
};
}
