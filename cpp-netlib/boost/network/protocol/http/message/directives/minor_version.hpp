#ifndef BOOST_NETWORK_PROTOCOL_HTTP_MESSAGE_DIRECTIVES_MINOR_VERSION_HPP_20101120
#define BOOST_NETWORK_PROTOCOL_HTTP_MESSAGE_DIRECTIVES_MINOR_VERSION_HPP_20101120

// Copyright 2010 Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/network/protocol/http/support/is_server.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/cstdint.hpp>

namespace boost {
namespace network {
namespace http {

template <class Tag>
struct basic_request;

struct minor_version_directive {
  boost::uint8_t minor_version;
  explicit minor_version_directive(boost::uint8_t minor_version)
      : minor_version(minor_version) {}
  template <class Tag>
  void operator()(basic_request<Tag>& request) const {
    request.http_version_minor = minor_version;
  }
};

inline minor_version_directive minor_version(boost::uint8_t minor_version_) {
  return minor_version_directive(minor_version_);
}

} /* http */

} /* network */

} /* boost */

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_MESSAGE_DIRECTIVES_MINOR_VERSION_HPP_20101120 \
          */
