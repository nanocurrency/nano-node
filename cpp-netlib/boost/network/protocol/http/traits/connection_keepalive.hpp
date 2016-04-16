//            Copyright (c) Dean Michael Berris 2010.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_NETWORK_PROTOCOL_HTTP_TRAITS_CONECTION_KEEPALIVE_20091218
#define BOOST_NETWORK_PROTOCOL_HTTP_TRAITS_CONECTION_KEEPALIVE_20091218

#include <boost/network/protocol/http/tags.hpp>
#include <boost/network/protocol/http/support/is_keepalive.hpp>

namespace boost {
namespace network {
namespace http {

template <class Tag>
struct connection_keepalive : is_keepalive<Tag> {};

} /* http */

} /* network */

} /* boost */

#endif  // BOOST_NETWORK_PROTOCOL_HTTP_TRAITS_CONECTION_KEEPALIVE_20091218
