// Copyright 2011 Dean Michael Berris &lt;mikhailberis@gmail.com&gt;.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE HTTP 1.1 Get Streaming Test
#include <boost/network/include/http/client.hpp>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include "client_types.hpp"

namespace net = boost::network;
namespace http = boost::network::http;

struct body_handler {

  explicit body_handler(std::string& body) : body(body) {}

  BOOST_NETWORK_HTTP_BODY_CALLBACK(operator(), range, error) {
    body.append(boost::begin(range), boost::end(range));
  }

  std::string& body;
};

BOOST_AUTO_TEST_CASE_TEMPLATE(http_client_get_streaming_test, client,
                              async_only_client_types) {
  typename client::request request("http://www.boost.org");
  typename client::response response;
  typename client::string_type body_string;
  typename client::string_type dummy_body;
  body_handler handler_instance(body_string);
  {
    client client_;
    BOOST_CHECK_NO_THROW(response = client_.get(request, handler_instance));
    typename net::headers_range<typename client::response>::type range =
        headers(response)["Content-Type"];
    BOOST_CHECK(!boost::empty(range));
    BOOST_CHECK_EQUAL(body(response).size(), 0u);
    BOOST_CHECK_EQUAL(response.version().substr(0, 7), std::string("HTTP/1."));
    BOOST_CHECK_EQUAL(response.status(), 200u);
    BOOST_CHECK_EQUAL(response.status_message(), std::string("OK"));
    dummy_body = body(response);
  }
  BOOST_CHECK(dummy_body == typename client::string_type());
}
