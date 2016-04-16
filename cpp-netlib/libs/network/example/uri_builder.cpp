//            Copyright (c) Glyn Matthews 2011.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <iostream>
#include <boost/network/uri.hpp>
#include <boost/network/uri/uri_io.hpp>

using namespace boost::network;

int main(int argc, char *argv[]) {

  uri::uri url;
  url << uri::scheme("http") << uri::host("www.github.com")
      << uri::path("/cpp-netlib");
  std::cout << url << std::endl;

  return 0;
}
