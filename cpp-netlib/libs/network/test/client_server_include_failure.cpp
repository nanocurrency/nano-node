//            Copyright (c) Glyn Matthews 2010.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE Client and server includes
#include <boost/test/unit_test.hpp>

//
// The problem here is a bizarre compilation failure in including
// these two files, and instantiating a client.  It's described at
// http://github.com/cpp-netlib/cpp-netlib/issues#issue/13
//
#include <boost/network/protocol/http/client.hpp>
#include <boost/network/protocol/http/server.hpp>

BOOST_AUTO_TEST_CASE(test1) {
  typedef boost::network::http::basic_client<
      boost::network::http::tags::http_keepalive_8bit_udp_resolve, 1, 1>
      http_client;
  http_client client;
}
