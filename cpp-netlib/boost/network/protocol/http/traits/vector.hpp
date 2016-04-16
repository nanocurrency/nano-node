#ifndef BOOST_NETWORK_PROTOCOL_HTTP_TRAITS_VECTOR_HPP_20101019
#define BOOST_NETWORK_PROTOCOL_HTTP_TRAITS_VECTOR_HPP_20101019

// Copyright (c) Dean Michael Berris 2010.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/network/traits/vector.hpp>
#include <vector>

namespace boost {
namespace network {

template <>
struct vector<http::tags::http_server> {

  template <class Type>
  struct apply {
    typedef std::vector<Type> type;
  };
};

template <>
struct vector<http::tags::http_async_server> {

  template <class Type>
  struct apply {
    typedef std::vector<Type> type;
  };
};

} /* network */

} /* boost */

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_TRAITS_VECTOR_HPP_20101019 */
