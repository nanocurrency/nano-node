#ifndef BOOST_NETWORK_SUPPORT_SYNC_ONLY_HPP_20100927
#define BOOST_NETWORK_SUPPORT_SYNC_ONLY_HPP_20100927

//          Copyright Dean Michael Berris 2010.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <boost/mpl/replace.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/mpl/inherit_linearly.hpp>
#include <boost/network/tags.hpp>
#include <boost/mpl/placeholders.hpp>

namespace boost {
namespace network {

template <class Tag>
struct sync_only
    : mpl::inherit_linearly<
          typename mpl::replace_if<typename tags::components<Tag>::type,
                                   is_same<mpl::placeholders::_, tags::async>,
                                   tags::sync>::type,
          mpl::inherit<mpl::placeholders::_1, mpl::placeholders::_2> > {};

} /* network */

} /* boost */

#endif /* BOOST_NETWORK_SUPPORT_SYNC_ONLY_HPP_20100927 */
