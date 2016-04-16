//            Copyright (c) Glyn Matthews 2011, 2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef __BOOST_NETWORK_URI_DIRECTIVES_AUTHORITY_INC__
#define __BOOST_NETWORK_URI_DIRECTIVES_AUTHORITY_INC__

namespace boost {
namespace network {
namespace uri {
struct authority_directive {

  explicit authority_directive(const std::string &authority)
      : authority(authority) {}

  template <class Uri>
  void operator()(Uri &uri) const {
    uri.append(authority);
  }

  std::string authority;
};

inline authority_directive authority(const std::string &authority) {
  return authority_directive(authority);
}
}  // namespace uri
}  // namespace network
}  // namespace boost

#endif  // __BOOST_NETWORK_URI_DIRECTIVES_AUTHORITY_INC__
