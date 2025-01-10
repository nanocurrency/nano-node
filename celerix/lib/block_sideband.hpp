#pragma once

#include <celerix/lib/epoch.hpp>
#include <celerix/lib/fwd.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/timer.hpp>

#include <cstdint>
#include <memory>

namespace celerix
{
class block_details
{
	static_assert (std::is_same<std::underlying_type<celerix::epoch>::type, uint8_t> (), "Epoch enum is not the proper type");
	static_assert (static_cast<uint8_t> (celerix::epoch::max) < (1 << 5), "Epoch max is too large for the sideband");

public:
	block_details () = default;
	block_details (celerix::epoch const epoch_a, bool const is_send_a, bool const is_receive_a, bool const is_epoch_a);
	static constexpr size_t size ()
	{
		return 1;
	}
	bool operator== (block_details const & other_a) const;
	void serialize (celerix::stream &) const;
	bool deserialize (celerix::stream &);
	celerix::epoch epoch{ celerix::epoch::epoch_0 };
	bool is_send{ false };
	bool is_receive{ false };
	bool is_epoch{ false };

private:
	uint8_t packed () const;
	void unpack (uint8_t);

public: // Logging
	void operator() (celerix::object_stream &) const;
};

std::string state_subtype (celerix::block_details const);

class block_sideband final
{
public:
	block_sideband () = default;
	block_sideband (celerix::account const &, celerix::block_hash const &, celerix::amount const &, uint64_t const, celerix::seconds_t const local_timestamp, celerix::block_details const &, celerix::epoch const source_epoch_a);
	block_sideband (celerix::account const &, celerix::block_hash const &, celerix::amount const &, uint64_t const, celerix::seconds_t const local_timestamp, celerix::epoch const epoch_a, bool const is_send, bool const is_receive, bool const is_epoch, celerix::epoch const source_epoch_a);
	void serialize (celerix::stream &, celerix::block_type) const;
	bool deserialize (celerix::stream &, celerix::block_type);
	static size_t size (celerix::block_type);
	celerix::block_hash successor{ 0 };
	celerix::account account{};
	celerix::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
	celerix::block_details details;
	celerix::epoch source_epoch{ celerix::epoch::epoch_0 };

public: // Logging
	void operator() (celerix::object_stream &) const;
};
} // namespace celerix
