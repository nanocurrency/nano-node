#include <bits/stdint-uintn.h>                  // for uint64_t
#include <cstddef>                             // for size_t
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/asio/ip/basic_endpoint.hpp>     // for basic_endpoint
#include <boost/asio/ip/impl/address.ipp>       // for address::address
#include <boost/multi_index/ordered_index_fwd.hpp>
#include <boost/multi_index/hashed_index_fwd.hpp>
#include <boost/multi_index/hashed_index.hpp>   // for hashed_unique
#include <boost/multi_index/identity_fwd.hpp>   // for multi_index
#include <boost/multi_index/indexed_by.hpp>     // for indexed_by
#include <boost/multi_index/ordered_index.hpp>  // for ordered_non_unique
#include <boost/multi_index/tag.hpp>            // for tag
#include <boost/multi_index_container.hpp>      // for multi_index_container
#include <chrono>                               // for hours, steady_clock
#include <memory>                               // for unique_ptr
#include <nano/node/common.hpp>                 // for tcp_endpoint
#include <string>                               // for string
#include <type_traits>                          // for declval
#include "nano/lib/locks.hpp"                   // for mutex

namespace boost { namespace multi_index { template <class Class, typename Type, Type Class::*PtrToMember> struct member; } }
namespace nano { class container_info_component; }

namespace mi = boost::multi_index;

namespace nano
{
class peer_exclusion final
{
	class item final
	{
	public:
		item () = delete;
		std::chrono::steady_clock::time_point exclude_until;
		decltype (std::declval<nano::tcp_endpoint> ().address ()) address;
		uint64_t score;
	};

	// clang-format off
	class tag_endpoint {};
	class tag_exclusion {};
	// clang-format on

public:
	// clang-format off
	using ordered_endpoints = boost::multi_index_container<peer_exclusion::item,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_exclusion>,
			mi::member<peer_exclusion::item, std::chrono::steady_clock::time_point, &peer_exclusion::item::exclude_until>>,
		mi::hashed_unique<mi::tag<tag_endpoint>,
			mi::member<peer_exclusion::item, decltype(peer_exclusion::item::address), &peer_exclusion::item::address>>>>;
	// clang-format on

private:
	ordered_endpoints peers;
	mutable nano::mutex mutex;

public:
	constexpr static size_t size_max = 5000;
	constexpr static double peers_percentage_limit = 0.5;
	constexpr static uint64_t score_limit = 2;
	constexpr static std::chrono::hours exclude_time_hours = std::chrono::hours (1);
	constexpr static std::chrono::hours exclude_remove_hours = std::chrono::hours (24);

	uint64_t add (nano::tcp_endpoint const &, size_t const);
	bool check (nano::tcp_endpoint const &);
	void remove (nano::tcp_endpoint const &);
	size_t limited_size (size_t const) const;
	size_t size () const;

	friend class telemetry_remove_peer_different_genesis_Test;
	friend class telemetry_remove_peer_different_genesis_udp_Test;
	friend class telemetry_remove_peer_invalid_signature_Test;
	friend class peer_exclusion_validate_Test;
};
std::unique_ptr<container_info_component> collect_container_info (peer_exclusion const & excluded_peers, std::string const & name);
}
