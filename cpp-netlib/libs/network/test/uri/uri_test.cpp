// Copyright 2009, 2010, 2011 Dean Michael Berris, Jeroen Habraken, Glyn
// Matthews.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt of copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE URL Test
#include <boost/config/warning_disable.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/network/uri.hpp>
#include <boost/network/uri/uri.hpp>
#include <boost/network/uri/uri_io.hpp>
#include <boost/range/algorithm/equal.hpp>
#include <boost/scoped_ptr.hpp>
#include <map>
#include <set>
#include <boost/unordered_set.hpp>

using namespace boost::network;

BOOST_AUTO_TEST_CASE(basic_uri_scheme_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::scheme(instance), "http");
}

BOOST_AUTO_TEST_CASE(basic_uri_user_info_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::user_info(instance), "");
}

BOOST_AUTO_TEST_CASE(basic_uri_host_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::host(instance), "www.example.com");
}

BOOST_AUTO_TEST_CASE(basic_uri_port_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::port(instance), "");
}

BOOST_AUTO_TEST_CASE(basic_uri_path_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::path(instance), "/");
}

BOOST_AUTO_TEST_CASE(basic_uri_query_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::query(instance), "");
}

BOOST_AUTO_TEST_CASE(basic_uri_fragment_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::fragment(instance), "");
}

BOOST_AUTO_TEST_CASE(basic_uri_value_semantics_test) {
  uri::uri original;
  uri::uri assigned;
  assigned = original;
  BOOST_CHECK(original == assigned);
  assigned = "http://www.example.com/";
  BOOST_CHECK(original != assigned);
  uri::uri copy(assigned);
  BOOST_CHECK(copy == assigned);
}

BOOST_AUTO_TEST_CASE(basic_uri_range_scheme_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(instance.scheme_range());
  BOOST_CHECK(instance.begin() == boost::begin(instance.scheme_range()));
  BOOST_CHECK(boost::equal(instance.scheme_range(), boost::as_literal("http")));
}

BOOST_AUTO_TEST_CASE(basic_uri_range_user_info_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(!instance.user_info_range());
  BOOST_CHECK(boost::begin(instance.host_range()) ==
              boost::begin(instance.user_info_range()));
  BOOST_CHECK(boost::begin(instance.host_range()) ==
              boost::end(instance.user_info_range()));
}

BOOST_AUTO_TEST_CASE(basic_uri_range_host_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(instance.host_range());
  BOOST_CHECK(boost::equal(instance.host_range(),
                           boost::as_literal("www.example.com")));
}

BOOST_AUTO_TEST_CASE(basic_uri_range_port_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(!instance.port_range());
  BOOST_CHECK(boost::end(instance.host_range()) ==
              boost::begin(instance.port_range()));
  BOOST_CHECK(boost::end(instance.host_range()) ==
              boost::end(instance.port_range()));
}

BOOST_AUTO_TEST_CASE(basic_uri_range_path_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(instance.path_range());
  BOOST_CHECK(boost::equal(instance.path_range(), boost::as_literal("/")));
  BOOST_CHECK(instance.end() == boost::end(instance.path_range()));
}

BOOST_AUTO_TEST_CASE(basic_uri_range_query_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(!instance.query_range());
  BOOST_CHECK(instance.end() == boost::begin(instance.query_range()));
  BOOST_CHECK(instance.end() == boost::end(instance.query_range()));
}

BOOST_AUTO_TEST_CASE(basic_uri_range_fragment_test) {
  uri::uri instance("http://www.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(!instance.fragment_range());
  BOOST_CHECK(instance.end() == boost::begin(instance.fragment_range()));
  BOOST_CHECK(instance.end() == boost::end(instance.fragment_range()));
}

BOOST_AUTO_TEST_CASE(full_uri_scheme_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::scheme(instance), "http");
}

BOOST_AUTO_TEST_CASE(full_uri_user_info_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::user_info(instance), "user:password");
}

BOOST_AUTO_TEST_CASE(full_uri_host_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::host(instance), "www.example.com");
}

BOOST_AUTO_TEST_CASE(full_uri_port_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::port(instance), "80");
  BOOST_CHECK(uri::port_us(instance));
  BOOST_CHECK_EQUAL(uri::port_us(instance).get(), 80);
}

BOOST_AUTO_TEST_CASE(full_uri_path_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::path(instance), "/path");
}

BOOST_AUTO_TEST_CASE(full_uri_query_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::query(instance), "query");
}

BOOST_AUTO_TEST_CASE(full_uri_fragment_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::fragment(instance), "fragment");
}

BOOST_AUTO_TEST_CASE(full_uri_range_scheme_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(instance.scheme_range());
  BOOST_CHECK(instance.begin() == boost::begin(instance.scheme_range()));
  BOOST_CHECK(boost::equal(instance.scheme_range(), boost::as_literal("http")));
}

BOOST_AUTO_TEST_CASE(full_uri_range_user_info_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(instance.user_info_range());
  BOOST_CHECK(boost::equal(instance.user_info_range(),
                           boost::as_literal("user:password")));
}

BOOST_AUTO_TEST_CASE(full_uri_range_host_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(instance.host_range());
  BOOST_CHECK(boost::equal(instance.host_range(),
                           boost::as_literal("www.example.com")));
}

BOOST_AUTO_TEST_CASE(full_uri_range_port_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(instance.port_range());
  BOOST_CHECK(boost::equal(instance.port_range(), boost::as_literal("80")));
}

BOOST_AUTO_TEST_CASE(full_uri_range_path_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(instance.path_range());
  BOOST_CHECK(boost::equal(instance.path_range(), boost::as_literal("/path")));
}

BOOST_AUTO_TEST_CASE(full_uri_range_query_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(instance.query_range());
  BOOST_CHECK(boost::equal(instance.query_range(), boost::as_literal("query")));
}

BOOST_AUTO_TEST_CASE(full_uri_range_fragment_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(instance.fragment_range());
  BOOST_CHECK(
      boost::equal(instance.fragment_range(), boost::as_literal("fragment")));
  BOOST_CHECK(instance.end() == boost::end(instance.fragment_range()));
}

BOOST_AUTO_TEST_CASE(mailto_test) {
  uri::uri instance("mailto:john.doe@example.com");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::scheme(instance), "mailto");
  BOOST_CHECK_EQUAL(uri::path(instance), "john.doe@example.com");
}

BOOST_AUTO_TEST_CASE(file_test) {
  uri::uri instance("file:///bin/bash");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::scheme(instance), "file");
  BOOST_CHECK_EQUAL(uri::path(instance), "/bin/bash");
}

BOOST_AUTO_TEST_CASE(xmpp_test) {
  uri::uri instance(
      "xmpp:example-node@example.com?message;subject=Hello%20World");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::scheme(instance), "xmpp");
  BOOST_CHECK_EQUAL(uri::path(instance), "example-node@example.com");
  BOOST_CHECK_EQUAL(uri::query(instance), "message;subject=Hello%20World");
}

BOOST_AUTO_TEST_CASE(ipv4_address_test) {
  uri::uri instance("http://129.79.245.252/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::scheme(instance), "http");
  BOOST_CHECK_EQUAL(uri::host(instance), "129.79.245.252");
  BOOST_CHECK_EQUAL(uri::path(instance), "/");
}

BOOST_AUTO_TEST_CASE(ipv4_loopback_test) {
  uri::uri instance("http://127.0.0.1/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::scheme(instance), "http");
  BOOST_CHECK_EQUAL(uri::host(instance), "127.0.0.1");
  BOOST_CHECK_EQUAL(uri::path(instance), "/");
}

BOOST_AUTO_TEST_CASE(ipv6_address_test_1) {
  uri::uri instance("http://[1080:0:0:0:8:800:200C:417A]/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::scheme(instance), "http");
  BOOST_CHECK_EQUAL(uri::host(instance), "[1080:0:0:0:8:800:200C:417A]");
  BOOST_CHECK_EQUAL(uri::path(instance), "/");
}

BOOST_AUTO_TEST_CASE(ipv6_address_test_2) {
  uri::uri instance("http://[2001:db8:85a3:8d3:1319:8a2e:370:7348]/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::scheme(instance), "http");
  BOOST_CHECK_EQUAL(uri::host(instance),
                    "[2001:db8:85a3:8d3:1319:8a2e:370:7348]");
  BOOST_CHECK_EQUAL(uri::path(instance), "/");
}

// BOOST_AUTO_TEST_CASE(ipv6_loopback_test) {
//    uri::uri instance("http://[::1]/");
//    BOOST_REQUIRE(uri::valid(instance));
//    BOOST_CHECK_EQUAL(uri::scheme(instance), "http");
//    BOOST_CHECK_EQUAL(uri::host(instance), "[::1]");
//    BOOST_CHECK_EQUAL(uri::path(instance), "/");
//}

BOOST_AUTO_TEST_CASE(ftp_test) {
  uri::uri instance("ftp://john.doe@ftp.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::scheme(instance), "ftp");
  BOOST_CHECK_EQUAL(uri::user_info(instance), "john.doe");
  BOOST_CHECK_EQUAL(uri::host(instance), "ftp.example.com");
  BOOST_CHECK_EQUAL(uri::path(instance), "/");
}

BOOST_AUTO_TEST_CASE(news_test) {
  uri::uri instance("news:comp.infosystems.www.servers.unix");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::scheme(instance), "news");
  BOOST_CHECK_EQUAL(uri::path(instance), "comp.infosystems.www.servers.unix");
}

BOOST_AUTO_TEST_CASE(tel_test) {
  uri::uri instance("tel:+1-816-555-1212");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::scheme(instance), "tel");
  BOOST_CHECK_EQUAL(uri::path(instance), "+1-816-555-1212");
}

BOOST_AUTO_TEST_CASE(encoded_uri_test) {
  uri::uri instance(
      "http://www.example.com/"
      "Path%20With%20%28Some%29%20Encoded%20Characters%21");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::scheme(instance), "http");
  BOOST_CHECK_EQUAL(uri::host(instance), "www.example.com");
  BOOST_CHECK_EQUAL(uri::path(instance),
                    "/Path%20With%20%28Some%29%20Encoded%20Characters%21");
  BOOST_CHECK_EQUAL(uri::decoded_path(instance),
                    "/Path With (Some) Encoded Characters!");
}

BOOST_AUTO_TEST_CASE(copy_constructor_test) {
  uri::uri instance("http://www.example.com/");
  uri::uri copy = instance;
  BOOST_CHECK_EQUAL(instance, copy);
}

BOOST_AUTO_TEST_CASE(assignment_test) {
  uri::uri instance("http://www.example.com/");
  uri::uri copy;
  copy = instance;
  BOOST_CHECK_EQUAL(instance, copy);
}

BOOST_AUTO_TEST_CASE(swap_test) {
  uri::uri instance("http://www.example.com/");
  uri::uri copy("http://www.example.org/");
  uri::swap(instance, copy);
  BOOST_CHECK_EQUAL(instance.string(), "http://www.example.org/");
  BOOST_CHECK_EQUAL(copy.string(), "http://www.example.com/");
}

BOOST_AUTO_TEST_CASE(equality_test) {
  uri::uri uri_1("http://www.example.com/");
  uri::uri uri_2("http://www.example.com/");
  BOOST_CHECK(uri_1 == uri_2);
}

BOOST_AUTO_TEST_CASE(equality_test_1) {
  uri::uri uri_1("http://www.example.com/");
  std::string uri_2("http://www.example.com/");
  BOOST_CHECK(uri_1 == uri_2);
}

BOOST_AUTO_TEST_CASE(equality_test_2) {
  std::string uri_1("http://www.example.com/");
  uri::uri uri_2("http://www.example.com/");
  BOOST_CHECK(uri_1 == uri_2);
}

BOOST_AUTO_TEST_CASE(equality_test_3) {
  uri::uri uri_1("http://www.example.com/");
  std::string uri_2("http://www.example.com/");
  BOOST_CHECK(uri_1 == uri_2.c_str());
}

BOOST_AUTO_TEST_CASE(equality_test_4) {
  std::string uri_1("http://www.example.com/");
  uri::uri uri_2("http://www.example.com/");
  BOOST_CHECK(uri_1.c_str() == uri_2);
}

BOOST_AUTO_TEST_CASE(inequality_test) {
  uri::uri uri_1("http://www.example.com/");
  uri::uri uri_2("http://www.example.com/");
  BOOST_CHECK(!(uri_1 != uri_2));
}

BOOST_AUTO_TEST_CASE(less_than_test) {
  // uri_1 is lexicographically less than uri_2
  uri::uri uri_1("http://www.example.com/");
  uri::uri uri_2("http://www.example.org/");
  BOOST_CHECK(uri_1 < uri_2);
}

BOOST_AUTO_TEST_CASE(username_test) {
  uri::uri instance("ftp://john.doe@ftp.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::username(instance), "john.doe");
}

BOOST_AUTO_TEST_CASE(pasword_test) {
  uri::uri instance("ftp://john.doe:password@ftp.example.com/");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::password(instance), "password");
}

BOOST_AUTO_TEST_CASE(hierarchical_part_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::hierarchical_part(instance),
                    "user:password@www.example.com:80/path");
}

BOOST_AUTO_TEST_CASE(partial_hierarchical_part_test) {
  uri::uri instance("http://www.example.com?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::hierarchical_part(instance), "www.example.com");
}

BOOST_AUTO_TEST_CASE(authority_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::authority(instance),
                    "user:password@www.example.com:80");
}

BOOST_AUTO_TEST_CASE(partial_authority_test) {
  uri::uri instance("http://www.example.com/path?query#fragment");
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK_EQUAL(uri::authority(instance), "www.example.com");
}

BOOST_AUTO_TEST_CASE(http_query_map_test) {
  uri::uri instance(
      "http://user:password@www.example.com:80/path?query=something#fragment");
  BOOST_REQUIRE(uri::valid(instance));

  std::map<std::string, std::string> queries;
  uri::query_map(instance, queries);
  BOOST_REQUIRE_EQUAL(queries.size(), std::size_t(1));
  BOOST_CHECK_EQUAL(queries.begin()->first, "query");
  BOOST_CHECK_EQUAL(queries.begin()->second, "something");
}

BOOST_AUTO_TEST_CASE(xmpp_query_map_test) {
  uri::uri instance(
      "xmpp:example-node@example.com?message;subject=Hello%20World");
  BOOST_REQUIRE(uri::valid(instance));

  std::map<std::string, std::string> queries;
  uri::query_map(instance, queries);
  BOOST_REQUIRE_EQUAL(queries.size(), std::size_t(2));
  BOOST_CHECK_EQUAL(queries.begin()->first, "message");
  BOOST_CHECK_EQUAL(queries.begin()->second, "");
  BOOST_CHECK_EQUAL((++queries.begin())->first, "subject");
  BOOST_CHECK_EQUAL((++queries.begin())->second, "Hello%20World");
}

BOOST_AUTO_TEST_CASE(range_test) {
  const std::string url("http://www.example.com/");
  uri::uri instance(url);
  BOOST_REQUIRE(uri::valid(instance));
  BOOST_CHECK(boost::equal(instance, url));
}

BOOST_AUTO_TEST_CASE(issue_67_test) {
  // https://github.com/cpp-netlib/cpp-netlib/issues/67
  const std::string site_name("http://www.google.com");
  uri::uri bar0;
  uri::uri bar1 = site_name;
  bar0 = site_name;
  BOOST_CHECK(uri::is_valid(bar0));
  BOOST_CHECK(uri::is_valid(bar1));
}

BOOST_AUTO_TEST_CASE(from_parts_1) {
  BOOST_CHECK_EQUAL(uri::uri("http://www.example.com/path?query#fragment"),
                    uri::from_parts(uri::uri("http://www.example.com"), "/path",
                                    "query", "fragment"));
}

BOOST_AUTO_TEST_CASE(from_parts_2) {
  BOOST_CHECK_EQUAL(
      uri::uri("http://www.example.com/path?query#fragment"),
      uri::from_parts("http://www.example.com", "/path", "query", "fragment"));
}

BOOST_AUTO_TEST_CASE(from_parts_3) {
  BOOST_CHECK_EQUAL(
      uri::uri("http://www.example.com/path?query"),
      uri::from_parts("http://www.example.com", "/path", "query"));
}

BOOST_AUTO_TEST_CASE(from_parts_4) {
  BOOST_CHECK_EQUAL(uri::uri("http://www.example.com/path"),
                    uri::from_parts("http://www.example.com", "/path"));
}

BOOST_AUTO_TEST_CASE(from_file) {
  boost::filesystem::path path("/a/path/to/a/file.txt");
  BOOST_CHECK_EQUAL(uri::uri("file:///a/path/to/a/file.txt"),
                    uri::from_file(path));
}

BOOST_AUTO_TEST_CASE(issue_104_test) {
  // https://github.com/cpp-netlib/cpp-netlib/issues/104
  boost::scoped_ptr<uri::uri> instance(new uri::uri("http://www.example.com/"));
  uri::uri copy = *instance;
  instance.reset();
  BOOST_CHECK_EQUAL(uri::scheme(copy), "http");
}

BOOST_AUTO_TEST_CASE(uri_set_test) {
  std::set<uri::uri> uri_set;
  uri_set.insert(uri::uri("http://www.example.com/"));
  BOOST_REQUIRE(!uri_set.empty());
  BOOST_CHECK_EQUAL((*uri_set.begin()), uri::uri("http://www.example.com/"));
}

BOOST_AUTO_TEST_CASE(uri_unordered_set_test) {
  boost::unordered_set<uri::uri> uri_set;
  uri_set.insert(uri::uri("http://www.example.com/"));
  BOOST_REQUIRE(!uri_set.empty());
  BOOST_CHECK_EQUAL((*uri_set.begin()), uri::uri("http://www.example.com/"));
}

BOOST_AUTO_TEST_CASE(issue_161_test) {
  uri::uri instance(
      "http://www.example.com/"
      "path?param1=-&param2=some+plus+encoded+text&param3=~");
  BOOST_REQUIRE(uri::valid(instance));

  std::map<std::string, std::string> queries;
  uri::query_map(instance, queries);
  BOOST_REQUIRE_EQUAL(queries.size(), std::size_t(3));
  BOOST_CHECK_EQUAL(queries["param1"], "-");
  BOOST_CHECK_EQUAL(queries["param2"], "some+plus+encoded+text");
  BOOST_CHECK_EQUAL(queries["param3"], "~");
  BOOST_CHECK_EQUAL(uri::decoded(queries["param2"]), "some plus encoded text");
}

BOOST_AUTO_TEST_CASE(issue_364_test) {
  uri::uri instance;
  uri::schemes::http(instance) << uri::host("my.awesome.server.com");
  BOOST_CHECK_EQUAL("my.awesome.server.com", uri::authority(instance));
}

BOOST_AUTO_TEST_CASE(issue_447_test) {
  uri::uri instance("http://[www.foo.com/");
  BOOST_REQUIRE(!uri::valid(instance));
}

BOOST_AUTO_TEST_CASE(issue_499_test) {
  uri::uri instance(
      "http://www.example.com/path?param1&param2=&param3=value");
  BOOST_REQUIRE(uri::valid(instance));

  std::map<std::string, std::string> queries;
  uri::query_map(instance, queries);
  BOOST_REQUIRE_EQUAL(queries.size(), std::size_t(3));
  BOOST_CHECK_EQUAL(queries["param1"], "");
  BOOST_CHECK_EQUAL(queries["param2"], "");
  BOOST_CHECK_EQUAL(queries["param3"], "value");
}
