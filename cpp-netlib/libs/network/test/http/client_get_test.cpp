// Copyright 2010 Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE HTTP 1.0 Get Test
#include <boost/network/include/http/client.hpp>
#include <boost/test/unit_test.hpp>
#include "client_types.hpp"

namespace net = boost::network;
namespace http = boost::network::http;

BOOST_AUTO_TEST_CASE_TEMPLATE(http_client_get_test, client, client_types) {
  typename client::request request("http://www.boost.org");
  client client_;
  typename client::response response;
  BOOST_REQUIRE_NO_THROW(response = client_.get(request));
  typename net::headers_range<typename client::response>::type range =
      headers(response)["Content-Type"];
  BOOST_CHECK(!boost::empty(range));
  BOOST_REQUIRE_NO_THROW(BOOST_CHECK(body(response).size() != 0));
  BOOST_CHECK_EQUAL(response.version().substr(0, 7), std::string("HTTP/1."));
  BOOST_CHECK_EQUAL(response.status(), 200u);
  BOOST_CHECK_EQUAL(response.status_message(), std::string("OK"));
}

#ifdef BOOST_NETWORK_ENABLE_HTTPS

BOOST_AUTO_TEST_CASE_TEMPLATE(https_client_get_test, client, client_types) {
  typename client::request request("https://www.google.com/");
  client client_;
  typename client::response response_ = client_.get(request);
  typename net::headers_range<typename client::response>::type range =
      headers(response_)["Content-Type"];
  BOOST_CHECK(boost::begin(range) != boost::end(range));
  BOOST_CHECK(body(response_).size() != 0);
}

#endif

BOOST_AUTO_TEST_CASE_TEMPLATE(http_temp_client_get_test, client, client_types) {
  typename client::request request("http://www.google.co.kr");
  typename client::response response;
  BOOST_REQUIRE_NO_THROW(response = client().get(request));
  typename net::headers_range<typename client::response>::type range =
      headers(response)["Content-Type"];
  BOOST_CHECK(!boost::empty(range));
  BOOST_REQUIRE_NO_THROW(BOOST_CHECK(body(response).size() != 0));
  BOOST_CHECK_EQUAL(response.version().substr(0, 7), std::string("HTTP/1."));
  BOOST_CHECK_EQUAL(response.status(), 200u);
  BOOST_CHECK_EQUAL(response.status_message(), std::string("OK"));
}
