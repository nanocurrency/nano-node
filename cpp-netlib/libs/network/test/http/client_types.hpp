#ifndef CLIENT_TYPES_ROOWQCLE
#define CLIENT_TYPES_ROOWQCLE

// Copyright 2010 Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include "tag_types.hpp"
#include <boost/mpl/joint_view.hpp>
#include <boost/mpl/bind.hpp>
#include <boost/mpl/transform.hpp>
#include <boost/mpl/remove_if.hpp>
#include <boost/mpl/int.hpp>
#include <boost/network/support/is_sync.hpp>

namespace mpl = boost::mpl;

template <unsigned major, unsigned minor>
struct client_adapter {
  template <class T1>
  struct apply {
    typedef boost::network::http::basic_client<T1, major, minor> type;
  };
};

typedef mpl::transform<tag_types, client_adapter<1, 0> >::type client_1_0;

typedef mpl::transform<tag_types, client_adapter<1, 1> >::type client_1_1;

typedef mpl::joint_view<client_1_0, client_1_1>::type client_types;

typedef mpl::joint_view<
    mpl::transform<mpl::remove_if<tag_types, boost::network::is_sync<
                                                 boost::mpl::_> >::type,
                   client_adapter<1, 0> >::type,
    mpl::transform<mpl::remove_if<tag_types, boost::network::is_sync<
                                                 boost::mpl::_> >::type,
                   client_adapter<1, 1> >::type>::type async_only_client_types;

#endif /* CLIENT_TYPES_ROOWQCLE */
