//            Copyright (c) Glyn Matthews 2011, 2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef __BOOST_NETWORK_URI_DIRECTIVES_PORT_INC__
#define __BOOST_NETWORK_URI_DIRECTIVES_PORT_INC__

#include <boost/cstdint.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/range/begin.hpp>
#include <boost/range/end.hpp>

namespace boost {
namespace network {
namespace uri {
struct port_directive {

  explicit port_directive(const std::string &port) : port(port) {}

  explicit port_directive(boost::uint16_t port)
      : port(boost::lexical_cast<std::string>(port)) {}

  template <class Uri>
  void operator()(Uri &uri) const {
    uri.append(":");
    uri.append(port);
  }

  std::string port;
};

inline port_directive port(const std::string &port) {
  return port_directive(port);
}

inline port_directive port(boost::uint16_t port) {
  return port_directive(port);
}
}  // namespace uri
}  // namespace network
}  // namespace boost

#endif  // __BOOST_NETWORK_URI_DIRECTIVES_PORT_INC__
