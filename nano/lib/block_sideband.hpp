#pragma once

#include <nano/lib/block_type.hpp>
#include <nano/lib/epoch.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/timer.hpp>

#include <cstdint>
#include <memory>

namespace nano
{
class object_stream;
}

namespace nano
{
class block_details
{
	static_assert (std::is_same<std::underlying_type<nano::epoch>::type, uint8_t> (), "Epoch enum is not the proper type");
	static_assert (static_cast<uint8_t> (nano::epoch::max) < (1 << 5), "Epoch max is too large for the sideband");

public:
	block_details () = default;
	block_details (nano::epoch const epoch_a, bool const is_send_a, bool const is_receive_a, bool const is_epoch_a);
	static constexpr size_t size ()
	{
		return 1;
	}
	bool operator== (block_details const & other_a) const;
	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);
	nano::epoch epoch{ nano::epoch::epoch_0 };
	bool is_send{ false };
	bool is_receive{ false };
	bool is_epoch{ false };

private:
	uint8_t packed () const;
	void unpack (uint8_t);

public: // Logging
	void operator() (nano::object_stream &) const;
};

std::string state_subtype (nano::block_details const);

class block_sideband final
{
public:
	block_sideband () = default;
	block_sideband (nano::account const &, nano::block_hash const &, nano::amount const &, uint64_t const, nano::seconds_t const local_timestamp, nano::block_details const &, nano::epoch const source_epoch_a);
	block_sideband (nano::account const &, nano::block_hash const &, nano::amount const &, uint64_t const, nano::seconds_t const local_timestamp, nano::epoch const epoch_a, bool const is_send, bool const is_receive, bool const is_epoch, nano::epoch const source_epoch_a);
	void serialize (nano::stream &, nano::block_type) const;
	bool deserialize (nano::stream &, nano::block_type);
	static size_t size (nano::block_type);
	nano::block_hash successor{ 0 };
	nano::account account{};
	nano::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
	nano::block_details details;
	nano::epoch source_epoch{ nano::epoch::epoch_0 };

public: // Logging
	void operator() (nano::object_stream &) const;
};
} // namespace nano
