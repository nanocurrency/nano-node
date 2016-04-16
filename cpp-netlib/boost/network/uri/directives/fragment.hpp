//            Copyright (c) Glyn Matthews 2011, 2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef __BOOST_NETWORK_URI_DIRECTIVES_FRAGMENT_INC__
#define __BOOST_NETWORK_URI_DIRECTIVES_FRAGMENT_INC__

#include <boost/network/uri/encode.hpp>
#include <boost/range/begin.hpp>
#include <boost/range/end.hpp>

namespace boost {
namespace network {
namespace uri {
struct fragment_directive {

  explicit fragment_directive(const std::string &fragment)
      : fragment(fragment) {}

  template <class Uri>
  void operator()(Uri &uri) const {
    uri.append("#");
    uri.append(fragment);
  }

  std::string fragment;
};

inline fragment_directive fragment(const std::string &fragment) {
  return fragment_directive(fragment);
}
}  // namespace uri
}  // namespace network
}  // namespace boost

#endif  // __BOOST_NETWORK_URI_DIRECTIVES_FRAGMENT_INC__
