// Copyright 2011 Dean Michael Berris &lt;mikhailberis@gmail.com&gt;.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_NETWORK_NO_LIB
#include <boost/network/include/http/client.hpp>

int main(int argc, char* argv[]) {
  using namespace boost;
  using namespace boost::network;
  http::client c;
  http::client::request req("http://www.boost.org/");
  try {
    http::client::response res = c.get(req);
  }
  catch (...) {
    // ignore the error, we just want to make sure
    // the interface works inlined.
  }
  return 0;
}
