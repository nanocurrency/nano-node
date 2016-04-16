
// Copyright 2010 Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE HTTP Request Linearize Test
#include <boost/network/protocol/http/request.hpp>
#include <boost/network/protocol/http/algorithms/linearize.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/mpl/list.hpp>
#include <iostream>
#include <iterator>

namespace http = boost::network::http;
namespace tags = boost::network::http::tags;
namespace mpl = boost::mpl;
namespace net = boost::network;

typedef mpl::list<tags::http_default_8bit_tcp_resolve,
                  tags::http_default_8bit_udp_resolve,
                  tags::http_async_8bit_tcp_resolve,
                  tags::http_async_8bit_udp_resolve> tag_types;

BOOST_AUTO_TEST_CASE_TEMPLATE(linearize_request, T, tag_types) {
  http::basic_request<T> request("http://www.boost.org");
  static char http_1_0_output[] =
      "GET / HTTP/1.0\r\n"
      "Host: www.boost.org\r\n"
      "Accept: */*\r\n"
      "Connection: Close\r\n"
      "\r\n";
  static char http_1_1_output[] =
      "GET / HTTP/1.1\r\n"
      "Host: www.boost.org\r\n"
      "Accept: */*\r\n"
      "Accept-Encoding: identity;q=1.0, *;q=0\r\n"
      "Connection: Close\r\n"
      "\r\n";
  typename http::basic_request<T>::string_type output_1_0;
  linearize(request, "GET", 1, 0, std::back_inserter(output_1_0));
  BOOST_CHECK_EQUAL(output_1_0, http_1_0_output);
  typename http::basic_request<T>::string_type output_1_1;
  linearize(request, "GET", 1, 1, std::back_inserter(output_1_1));
  BOOST_CHECK_EQUAL(output_1_1, http_1_1_output);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(linearize_request_override_headers, T,
                              tag_types) {
  http::basic_request<T> request("http://www.boost.org");
  // We can override the defaulted headers and test that here.
  request << net::header("Accept", "");
  static char http_1_0_no_accept_output[] =
      "GET / HTTP/1.0\r\n"
      "Host: www.boost.org\r\n"
      "Connection: Close\r\n"
      "\r\n";
  static char http_1_1_no_accept_output[] =
      "GET / HTTP/1.1\r\n"
      "Host: www.boost.org\r\n"
      "Accept-Encoding: identity;q=1.0, *;q=0\r\n"
      "Connection: Close\r\n"
      "\r\n";
  typename http::basic_request<T>::string_type output_1_0;
  linearize(request, "GET", 1, 0, std::back_inserter(output_1_0));
  BOOST_CHECK_EQUAL(output_1_0, http_1_0_no_accept_output);
  typename http::basic_request<T>::string_type output_1_1;
  linearize(request, "GET", 1, 1, std::back_inserter(output_1_1));
  BOOST_CHECK_EQUAL(output_1_1, http_1_1_no_accept_output);
}
