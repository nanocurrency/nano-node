
// Copyright 2010 (c) Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE HTTP message test
#include <boost/config/warning_disable.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/network/protocol/http/response.hpp>
#include <boost/network/protocol/http/request.hpp>
#include <boost/mpl/list.hpp>
#include <algorithm>

using namespace boost::network;

typedef boost::mpl::list<http::tags::http_default_8bit_tcp_resolve,
                         http::tags::http_default_8bit_udp_resolve,
                         http::tags::http_keepalive_8bit_tcp_resolve,
                         http::tags::http_keepalive_8bit_udp_resolve> tag_types;

struct fixtures {};

BOOST_FIXTURE_TEST_SUITE(http_message_test_suite, fixtures)

BOOST_AUTO_TEST_CASE_TEMPLATE(request_constructor_test, T, tag_types) {
  http::basic_request<T> request("http://boost.org");
  typedef typename http::basic_request<T>::string_type string_type;
  string_type host = http::host(request);
  boost::uint16_t port = http::port(request);
  string_type path = http::path(request);
  string_type query = http::query(request);
  string_type anchor = http::anchor(request);
  string_type protocol = http::protocol(request);
  BOOST_CHECK_EQUAL(host, "boost.org");
  BOOST_CHECK_EQUAL(port, 80u);
  BOOST_CHECK_EQUAL(path, "/");
  BOOST_CHECK_EQUAL(query, "");
  BOOST_CHECK_EQUAL(anchor, "");
  BOOST_CHECK_EQUAL(protocol, "http");
}

BOOST_AUTO_TEST_CASE_TEMPLATE(request_copy_constructor_test, T, tag_types) {
  http::basic_request<T> request("http://boost.org/handler.php");
  request << header("Content-Type", "text/plain") << body("Hello, World!");
  http::basic_request<T> copy(request);
  typedef typename http::basic_request<T>::string_type string_type;
  string_type orig_host = http::host(request), copy_host = http::host(copy);
  boost::uint16_t orig_port = http::port(request), copy_port = http::port(copy);
  string_type orig_path = http::path(request), copy_path = http::path(copy);
  string_type orig_body = body(request), copy_body = body(copy);
  BOOST_CHECK_EQUAL(orig_host, copy_host);
  BOOST_CHECK_EQUAL(orig_port, copy_port);
  BOOST_CHECK_EQUAL(orig_path, copy_path);
  BOOST_CHECK_EQUAL(orig_body, copy_body);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(request_assignment_test, T, tag_types) {
  http::basic_request<T> request("http://boost.org/handler.php");
  request << header("Content-Type", "text/plain") << body("Hello, World!");
  http::basic_request<T> copy;
  copy = request;
  typedef typename http::basic_request<T>::string_type string_type;
  string_type orig_host = http::host(request), copy_host = http::host(copy);
  boost::uint16_t orig_port = http::port(request), copy_port = http::port(copy);
  string_type orig_path = http::path(request), copy_path = http::path(copy);
  string_type orig_body = body(request), copy_body = body(copy);
  BOOST_CHECK_EQUAL(orig_host, copy_host);
  BOOST_CHECK_EQUAL(orig_port, copy_port);
  BOOST_CHECK_EQUAL(orig_path, copy_path);
  BOOST_CHECK_EQUAL(orig_body, copy_body);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(request_swap_test, T, tag_types) {
  boost::network::http::basic_request<T> request("http://boost.org/");
  boost::network::http::basic_request<T> other;
  swap(other, request);  // ADL
  typedef typename http::basic_request<T>::string_type string_type;
  string_type orig_host = http::host(request), orig_path = http::path(request),
              copy_host = http::host(other), copy_path = http::path(other);
  boost::uint16_t orig_port = http::port(request),
                  copy_port = http::port(request);
  BOOST_CHECK_EQUAL(orig_host, "");
  BOOST_CHECK_EQUAL(orig_port, 80u);
  BOOST_CHECK_EQUAL(orig_path, "/");
  BOOST_CHECK_EQUAL(copy_host, "boost.org");
  BOOST_CHECK_EQUAL(copy_port, 80u);
  BOOST_CHECK_EQUAL(copy_path, "/");
}

BOOST_AUTO_TEST_CASE_TEMPLATE(request_uri_directive_test, T, tag_types) {
  http::basic_request<T> request;
  request << http::uri("http://boost.org/");
  typename http::basic_request<T>::string_type uri_ = http::uri(request);
  BOOST_CHECK_EQUAL(uri_, "http://boost.org/");
}

BOOST_AUTO_TEST_CASE_TEMPLATE(response_constructor_test, T, tag_types) {
  http::basic_response<T> response;
  typename http::basic_response<T>::string_type body_ = body(response);
  BOOST_CHECK_EQUAL(body_, std::string());
}

BOOST_AUTO_TEST_CASE_TEMPLATE(response_copy_construct_test, T, tag_types) {
  using namespace http;
  http::basic_response<T> response;
  response << http::version("HTTP/1.1") << http::status(200u)
           << body("The quick brown fox jumps over the lazy dog")
           << http::status_message("OK");
  http::basic_response<T> copy(response);

  typename http::basic_response<T>::string_type version_orig =
                                                    version(response),
                                                version_copy = version(copy);
  BOOST_CHECK_EQUAL(version_orig, version_copy);
  boost::uint16_t status_orig = status(response), status_copy = status(copy);
  BOOST_CHECK_EQUAL(status_orig, status_copy);
  typename http::basic_response<T>::string_type status_message_orig =
                                                    status_message(response),
                                                status_message_copy =
                                                    status_message(copy),
                                                body_orig = body(response),
                                                body_copy = body(copy);
  BOOST_CHECK_EQUAL(status_message_orig, status_message_copy);
  BOOST_CHECK_EQUAL(body_orig, body_copy);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(response_assignment_construct_test, T,
                              tag_types) {
  http::basic_response<T> response;
  response << http::version("HTTP/1.1") << http::status(200)
           << http::status_message("OK")
           << body("The quick brown fox jumps over the lazy dog");
  http::basic_response<T> copy;
  copy = response;
  typedef typename http::basic_response<T>::string_type string_type;
  string_type version_orig = version(response), version_copy = version(copy);
  BOOST_CHECK_EQUAL(version_orig, version_copy);
  boost::uint16_t status_orig = status(response), status_copy = status(copy);
  BOOST_CHECK_EQUAL(status_orig, status_copy);
  string_type status_message_orig = status_message(response),
              status_message_copy = status_message(copy);
  BOOST_CHECK_EQUAL(status_message_orig, status_message_copy);
  string_type body_orig = body(response), body_copy = body(copy);
  BOOST_CHECK_EQUAL(body_orig, body_copy);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(response_swap_test, T, tag_types) {
  using namespace boost::network::http;
  http::basic_response<T> response;
  response << version("HTTP/1.1") << status(200) << status_message("OK")
           << body("RESPONSE");
  boost::network::http::basic_response<T> swapped;
  BOOST_REQUIRE_NO_THROW(swap(response, swapped));
  BOOST_CHECK_EQUAL(response.version(), std::string());
  BOOST_CHECK_EQUAL(response.status(), 0u);
  BOOST_CHECK_EQUAL(response.status_message(), std::string());
  typename http::basic_response<T>::string_type orig_body = body(response),
                                                swapped_body = body(swapped);
  BOOST_CHECK_EQUAL(orig_body, std::string());
  BOOST_CHECK_EQUAL(swapped.version(), std::string("HTTP/1.1"));
  BOOST_CHECK_EQUAL(swapped.status(), 200u);
  BOOST_CHECK_EQUAL(swapped.status_message(), std::string("OK"));
  BOOST_CHECK_EQUAL(swapped_body, std::string("RESPONSE"));
}

BOOST_AUTO_TEST_SUITE_END()
