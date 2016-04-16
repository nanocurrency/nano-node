//            Copyright (c) Glyn Matthews 2011, 2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef __BOOST_NETWORK_URI_DIRECTIVES_PATH_INC__
#define __BOOST_NETWORK_URI_DIRECTIVES_PATH_INC__

#include <boost/network/uri/encode.hpp>
#include <boost/range/as_literal.hpp>

namespace boost {
namespace network {
namespace uri {
struct path_directive {

  explicit path_directive(const std::string &path) : path(path) {}

  template <class Uri>
  void operator()(Uri &uri) const {
    uri.append(path);
  }

  std::string path;
};

struct encoded_path_directive {

  explicit encoded_path_directive(const std::string &path) : path(path) {}

  void operator()(uri &uri_) const {
    std::string encoded_path;
    encode(path, std::back_inserter(encoded_path));
    uri_.append(encoded_path);
  }

  std::string path;
};

inline path_directive path(const std::string &path) {
  return path_directive(path);
}

inline encoded_path_directive encoded_path(const std::string &path) {
  return encoded_path_directive(path);
}
}  // namespace uri
}  // namespace network
}  // namespace boost

#endif  // __BOOST_NETWORK_URI_DIRECTIVES_PATH_INC__
