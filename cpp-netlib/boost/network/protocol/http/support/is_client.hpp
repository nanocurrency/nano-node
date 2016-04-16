#ifndef BOOST_NETWORK_PROTOCOL_SUPPORT_IS_CLIENT_HPP_20101118
#define BOOST_NETWORK_PROTOCOL_SUPPORT_IS_CLIENT_HPP_20101118

// Copyright 2010 Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/network/protocol/http/tags.hpp>
#include <boost/utility/enable_if.hpp>

namespace boost {
namespace network {
namespace http {

template <class Tag, class Enable = void>
struct is_client : mpl::false_ {};

template <class Tag>
struct is_client<
    Tag, typename enable_if<typename Tag::is_client>::type> : mpl::true_ {};

} /* http */

} /* network */

} /* boost */

#endif /* BOOST_NETWORK_PROTOCOL_SUPPORT_IS_CLIENT_HPP_20101118 */
