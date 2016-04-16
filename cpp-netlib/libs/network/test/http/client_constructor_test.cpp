
// Copyright 2010 Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE HTTP 1.0 Client Constructor Test
#include <boost/network/include/http/client.hpp>
#include <boost/test/unit_test.hpp>
#include "client_types.hpp"

namespace http = boost::network::http;

BOOST_AUTO_TEST_CASE_TEMPLATE(http_client_constructor_test, client,
                              client_types) {
  typename client::options options;
  client instance;
  client instance2(
      options.io_service(boost::make_shared<boost::asio::io_service>()));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(http_cient_constructor_params_test, client,
                              client_types) {
  typename client::options options;
  client instance(options.follow_redirects(true).cache_resolved(true));
  client instance2(
      options.openssl_certificate("foo").openssl_verify_path("bar"));
  client instance3(
      options.openssl_certificate_file("foo").openssl_private_key_file("bar"));
  client instance4(
      options.follow_redirects(true)
          .io_service(boost::make_shared<boost::asio::io_service>())
          .cache_resolved(true));
}
