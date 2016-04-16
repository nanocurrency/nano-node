
// Copyright 2010 Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE HTTP Async Response Test
#include <boost/test/unit_test.hpp>
#include <boost/network/include/http/client.hpp>

namespace http = boost::network::http;

BOOST_AUTO_TEST_CASE(unready_state_response) {
  typedef http::basic_response<http::tags::http_async_8bit_udp_resolve>
      response;
  response r;
  BOOST_CHECK(!ready(r));
}
