//            Copyright (c) Glyn Matthews 2011.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE URI builder stream test
#include <boost/config/warning_disable.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/network/uri/uri.hpp>
#include <boost/network/uri/directives.hpp>
#include <boost/network/uri/uri_io.hpp>

using namespace boost::network;

BOOST_AUTO_TEST_CASE(builder_test) {
  uri::uri instance;
  instance << uri::scheme("http") << uri::host("www.example.com")
           << uri::path("/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL("http://www.example.com/", instance.string());
}

BOOST_AUTO_TEST_CASE(full_uri_builder_test) {
  uri::uri instance;
  instance << uri::scheme("http") << uri::user_info("user:password")
           << uri::host("www.example.com") << uri::port("80")
           << uri::path("/path") << uri::query("query")
           << uri::fragment("fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(
      "http://user:password@www.example.com:80/path?query#fragment",
      instance.string());
}

BOOST_AUTO_TEST_CASE(port_test) {
  uri::uri instance;
  instance << uri::scheme("http") << uri::host("www.example.com")
           << uri::port(8000) << uri::path("/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL("http://www.example.com:8000/", instance.string());
}

BOOST_AUTO_TEST_CASE(encoded_path_test) {
  uri::uri instance;
  instance << uri::scheme("http") << uri::host("www.example.com")
           << uri::port(8000)
           << uri::encoded_path("/Path With (Some) Encoded Characters!");
  ;
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(
      "http://www.example.com:8000/"
      "Path%20With%20%28Some%29%20Encoded%20Characters%21",
      instance.string());
}

BOOST_AUTO_TEST_CASE(query_test) {
  uri::uri instance;
  instance << uri::scheme("http") << uri::host("www.example.com")
           << uri::path("/") << uri::query("key", "value");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL("http://www.example.com/?key=value", instance.string());
}

BOOST_AUTO_TEST_CASE(query_2_test) {
  uri::uri instance;
  instance << uri::scheme("http") << uri::host("www.example.com")
           << uri::path("/") << uri::query("key1", "value1")
           << uri::query("key2", "value2");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL("http://www.example.com/?key1=value1&key2=value2",
                    instance.string());
}

BOOST_AUTO_TEST_CASE(fragment_test) {
  uri::uri instance;
  instance << uri::scheme("http") << uri::host("www.example.com")
           << uri::path("/") << uri::fragment("fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL("http://www.example.com/#fragment", instance.string());
}

BOOST_AUTO_TEST_CASE(from_base_test) {
  uri::uri base_uri("http://www.example.com");
  uri::uri instance;
  instance << base_uri << uri::path("/") << uri::fragment("fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL("http://www.example.com/#fragment", instance.string());
}

BOOST_AUTO_TEST_CASE(scheme_http_test) {
  uri::uri instance;
  instance << uri::schemes::http << uri::host("www.example.com")
           << uri::path("/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL("http://www.example.com/", instance.string());
}

BOOST_AUTO_TEST_CASE(scheme_https_test) {
  uri::uri instance;
  instance << uri::schemes::https << uri::host("www.example.com")
           << uri::path("/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL("https://www.example.com/", instance.string());
}

BOOST_AUTO_TEST_CASE(encoded_null_char_test) {
  // there is a potential bug in the way we process ranges if the
  // strings are null terminated.
  uri::uri instance;
  instance << uri::scheme("http") << uri::host("www.example.com")
           << uri::encoded_path("/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL("http://www.example.com/", instance.string());
}

BOOST_AUTO_TEST_CASE(mailto_builder_test) {
  uri::uri instance;
  instance << uri::scheme("mailto") << uri::path("cpp-netlib@example.com");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL("mailto:cpp-netlib@example.com", instance.string());
}
