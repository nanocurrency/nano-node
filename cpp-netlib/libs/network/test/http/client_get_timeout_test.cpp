
// Copyright 2010 Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE HTTP Client Get Timeout Test
#include <cstdlib>
#include <boost/network/include/http/client.hpp>
#include <boost/test/unit_test.hpp>
#include "client_types.hpp"
#include "http_test_server.hpp"

struct localhost_server_fixture {
  localhost_server_fixture() {
    if (!server.start()) {
      std::cout << "Failed to start HTTP server for test!" << std::endl;
      std::abort();
    }
  }

  ~localhost_server_fixture() {
    if (!server.stop()) {
      std::cout << "Failed to stop HTTP server for test!" << std::endl;
      std::abort();
    }
  }

  http_test_server server;
};

BOOST_GLOBAL_FIXTURE(localhost_server_fixture);

BOOST_AUTO_TEST_CASE_TEMPLATE(http_get_test_timeout_1_0, client, client_types) {
  typename client::request request("http://localhost:12121/");
  typename client::response response_;
  client client_;
  boost::uint16_t port_ = port(request);
  typename client::response::string_type temp;
  BOOST_CHECK_EQUAL(12121, port_);
  BOOST_CHECK_THROW(response_ = client_.get(request); temp = body(response_);
                    , std::exception);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(http_get_test_timeout_with_options, client, client_types) {
  typename client::request request("http://localhost:8000/cgi-bin/sleep.py?3");
  typename client::response response;
  typename client::options options;
  client client_(options.timeout(1));
  typename client::response::string_type temp;
  BOOST_CHECK_THROW(response = client_.get(request); temp = body(response);
                    , std::exception);
}

#ifdef BOOST_NETWORK_ENABLE_HTTPS

BOOST_AUTO_TEST_CASE_TEMPLATE(https_get_test_timeout_with_options, client, client_types) {
  typename client::request request("https://localhost:8000/cgi-bin/sleep.py?3");
  typename client::response response;
  typename client::options options;
  client client_(options.timeout(1));
  typename client::response::string_type temp;
  BOOST_CHECK_THROW(response = client_.get(request); temp = body(response);
                    , std::exception);
}

#endif
