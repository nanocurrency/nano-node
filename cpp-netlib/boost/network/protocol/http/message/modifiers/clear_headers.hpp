#ifndef BOOST_NETWORK_PROTOCOL_HTTP_MESSAGE_MODIFIERS_CLEAR_HEADER_HPP_20101128
#define BOOST_NETWORK_PROTOCOL_HTTP_MESSAGE_MODIFIERS_CLEAR_HEADER_HPP_20101128

// Copyright 2010 Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/network/protocol/http/support/client_or_server.hpp>
#include <boost/network/support/pod_or_normal.hpp>
#include <boost/network/support/is_async.hpp>
#include <boost/thread/future.hpp>

namespace boost {
namespace network {
namespace http {

template <class Tag>
inline void clear_headers_impl(basic_request<Tag>& request, tags::pod) {
  typedef typename basic_request<Tag>::headers_container_type headers_container;
  headers_container().swap(request.headers);
}

template <class Tag>
inline void clear_headers_impl(basic_request<Tag>& request, tags::normal) {
  request.headers(typename basic_request<Tag>::headers_container_type());
}

template <class Tag>
inline void clear_headers_impl(basic_request<Tag>& request, tags::client) {
  clear_headers_impl(request, typename pod_or_normal<Tag>::type());
}

template <class Tag>
inline void clear_headers_impl(basic_request<Tag>& request, tags::server) {
  typedef typename basic_request<Tag>::headers_container_type headers_container;
  headers_container().swap(request.headers);
}

template <class Tag>
inline void clear_headers(basic_request<Tag>& request) {
  clear_headers_impl(request, typename client_or_server<Tag>::type());
}

} /* http */

} /* network */

} /* boost */

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_MESSAGE_MODIFIERS_CLEAR_HEADER_HPP_20101128 \
          */
