#ifndef BOOST_NETWORK_PROTOCOL_HTTP_MESSAGE_TRAITS_STATUS_HPP_20100903
#define BOOST_NETWORK_PROTOCOL_HTTP_MESSAGE_TRAITS_STATUS_HPP_20100903

// Copyright Dean Michael Berris 2010.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/network/support/is_async.hpp>
#include <boost/cstdint.hpp>
#include <boost/network/tags.hpp>

namespace boost {
namespace network {
namespace http {

namespace traits {

template <class Tag>
struct unsupported_tag;

template <class Message>
struct status
    : mpl::if_<
          is_async<typename Message::tag>,
          boost::shared_future<boost::uint16_t>,
          typename mpl::if_<is_sync<typename Message::tag>, boost::uint16_t,
                            unsupported_tag<typename Message::tag> >::type> {};

} /* traits */

} /* http */
} /* network */
} /* boost */

#endif
