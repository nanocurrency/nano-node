//
//          Copyright Divye Kapoor 2008.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
//
// Changes by Kim Grasman 2008
// Changes by Dean Michael Berris 2008, 2010

#define BOOST_TEST_MODULE http 1.0 localhost tests

#include <boost/config/warning_disable.hpp>
#include <boost/config.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/network/include/http/client.hpp>
#include <boost/range.hpp>
#include <boost/cast.hpp>
#include <string>
#include <fstream>
#include <iostream>

#include "http_test_server.hpp"

using std::cout;
using std::endl;

namespace {
const std::string base_url = "http://localhost:8000";
const std::string cgi_url = base_url + "/cgi-bin/requestinfo.py";

struct running_server_fixture {
  // NOTE: Can't use BOOST_REQUIRE_MESSAGE here, as Boost.Test data structures
  // are not fully set up when the global fixture runs.
  running_server_fixture() {
    if (!server.start())
      cout << "Failed to start HTTP server for test!" << endl;
  }

  ~running_server_fixture() {
    if (!server.stop()) cout << "Failed to stop HTTP server for test!" << endl;
  }

  http_test_server server;
};

std::size_t readfile(std::ifstream& file, std::vector<char>& buffer) {
  using std::ios;

  std::istreambuf_iterator<char> src(file);
  std::istreambuf_iterator<char> eof;
  std::copy(src, eof, std::back_inserter(buffer));

  return buffer.size();
}

std::map<std::string, std::string> parse_headers(std::string const& body) {
  std::map<std::string, std::string> headers;

  std::istringstream stream(body);
  while (stream.good()) {
    std::string line;
    std::getline(stream, line);
    if (!stream.eof()) {
      std::size_t colon = line.find(':');
      if (colon != std::string::npos) {
        std::string header = line.substr(0, colon);
        std::string value = line.substr(colon + 2);
        headers[header] = value;
      }
    }
  }

  return headers;
}

std::string get_content_length(std::string const& content) {
  return boost::lexical_cast<std::string>(content.length());
}
}

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
// Uncomment the below if you're running Python pre-2.6. There was a bug
// in the Python HTTP server for earlier versions that causes this test
// case to fail.
// BOOST_AUTO_TEST_CASE_EXPECTED_FAILURES(text_query_preserves_crlf, 2);
#endif

BOOST_GLOBAL_FIXTURE(running_server_fixture);

BOOST_AUTO_TEST_CASE(body_test) {
  // Tests presence of body in http responses
  using namespace boost::network;
  http::client::request request_(base_url);
  http::client client_;
  http::client::response response_;
  BOOST_REQUIRE_NO_THROW(response_ = client_.get(request_));
  BOOST_CHECK(body(response_).size() != 0);
}

BOOST_AUTO_TEST_CASE(text_content_type_test) {
  // Tests correct parsing of the content-type header sent by the server
  using namespace boost::network;
  http::client::request request_(base_url);
  http::client client_;
  http::client::response response_;
  BOOST_REQUIRE_NO_THROW(response_ = client_.get(request_));
  BOOST_REQUIRE(headers(response_).count("Content-type") != 0);
  headers_range<http::client::response>::type range =
      headers(response_)["Content-type"];
  BOOST_CHECK(boost::begin(range)->first == "Content-type");
  BOOST_CHECK(boost::begin(range)->second == "text/html");
}

BOOST_AUTO_TEST_CASE(binary_content_type_test) {
  // Tests correct parsing of content-type for binary files such as .zip files
  using namespace boost::network;
  http::client::request request_(base_url + "/boost.jpg");
  http::client client_;
  http::client::response response_;
  BOOST_REQUIRE_NO_THROW(response_ = client_.get(request_));
  BOOST_REQUIRE(headers(response_).count("Content-type") != 0);
  headers_range<http::client::response>::type range =
      headers(response_)["Content-type"];
  BOOST_CHECK(boost::begin(range)->first == "Content-type");
  BOOST_CHECK(boost::begin(range)->second == "image/jpeg");
}

BOOST_AUTO_TEST_CASE(content_length_header_test) {
  // Uses the test.xml file to ensure that the file was received at the correct
  // length for a text encoding
  using namespace boost::network;
  http::client::request request_(base_url + "/test.xml");
  http::client client_;
  http::client::response response_;
  BOOST_REQUIRE_NO_THROW(response_ = client_.get(request_));
  BOOST_REQUIRE(headers(response_).count("Content-Length") != 0);
  headers_range<http::client::response>::type range =
      headers(response_)["Content-Length"];
  BOOST_CHECK_EQUAL(boost::begin(range)->first, "Content-Length");
  BOOST_CHECK_EQUAL(boost::begin(range)->second, "113");
  BOOST_CHECK(body(response_).size() != 0);
}

BOOST_AUTO_TEST_CASE(text_query_preserves_crlf) {
  // Tests proper transfer of a text file
  using namespace boost::network;
  http::client::request request_(base_url + "/test.xml");
  http::client client_;
  http::client::response response_;
  BOOST_REQUIRE_NO_THROW(response_ = client_.get(request_));

  http::client::response::string_type body_ = body(response_);
  BOOST_CHECK(body(response_).size() != 0);

  using std::ios;

  std::ifstream file("libs/network/test/server/test.xml",
                     ios::in | ios::binary);
  if (!file) {
    file.clear();
    file.open("server/test.xml", ios::in | ios::binary);
  }

  BOOST_REQUIRE_MESSAGE(file, "Could not open local test.xml");

  std::vector<char> memblock;
  std::size_t size = readfile(file, memblock);

  BOOST_CHECK(size != 0);
  BOOST_CHECK_EQUAL(body_.size(), size);

  if (body(response_).size() == size) {
    std::pair<std::vector<char>::iterator, std::string::const_iterator>
        diff_pos =
            std::mismatch(memblock.begin(), memblock.end(), body_.begin());
    BOOST_CHECK_EQUAL(
        boost::numeric_cast<std::size_t>(diff_pos.first - memblock.begin()),
        size);
  }
}

BOOST_AUTO_TEST_CASE(binary_file_query) {
  // Tests proper transfer of a binary image
  using namespace boost::network;
  http::client::request request_(base_url + "/boost.jpg");
  http::client client_;
  http::client::response response_;
  BOOST_REQUIRE_NO_THROW(response_ = client_.get(request_));

  http::client::response::string_type body_ = body(response_);
  BOOST_CHECK(body_.size() != 0);

  using std::ios;

  std::ifstream file("libs/network/test/server/boost.jpg",
                     ios::in | ios::binary);
  if (!file) {
    file.clear();
    file.open("server/boost.jpg", ios::in | ios::binary);
  }

  BOOST_REQUIRE_MESSAGE(file, "Could not open boost.jpg locally");

  std::vector<char> memblock;
  std::size_t size = readfile(file, memblock);

  BOOST_CHECK(size != 0);
  BOOST_CHECK_EQUAL(body_.size(), size);

  std::pair<std::vector<char>::iterator, std::string::const_iterator> diff_pos =
      std::mismatch(memblock.begin(), memblock.end(), body_.begin());
  BOOST_CHECK_EQUAL(
      boost::numeric_cast<std::size_t>(diff_pos.first - memblock.begin()),
      size);
}

BOOST_AUTO_TEST_CASE(cgi_query) {
  // Get a dynamic request with no Content-Length header
  // Ensure that we have a body
  using namespace boost::network;

  http::client::request req(cgi_url + "?query=1");
  http::client c;
  http::client::response r;
  BOOST_REQUIRE_NO_THROW(r = c.get(req));
  BOOST_CHECK(body(r).size() != 0);
  BOOST_CHECK(boost::empty(headers(r)["Content-Length"]));
}

BOOST_AUTO_TEST_CASE(cgi_multi_line_headers) {
  using namespace boost::network;

  http::client::request req(base_url + "/cgi-bin/multiline-header.py?query=1");
  http::client c;
  http::client::response r;
  BOOST_REQUIRE_NO_THROW(r = c.get(req));
  BOOST_CHECK(body(r).size() != 0);
  BOOST_CHECK(boost::empty(headers(r)["Content-Type"]));
  headers_range<http::client::response>::type range =
      headers(r)["X-CppNetlib-Test"];
  BOOST_REQUIRE(boost::begin(range) != boost::end(range));
  BOOST_REQUIRE(distance(range) == 2);
  BOOST_CHECK_EQUAL(boost::begin(range)->second,
                    std::string("multi-line-header"));
  BOOST_CHECK_EQUAL((++boost::begin(range))->second,
                    std::string("that-should-concatenate"));
}

BOOST_AUTO_TEST_CASE(file_not_found) {
  // Request for a non existing file.
  // Ensure that we have a body even in the presence of an error response
  using namespace boost::network;

  http::client::request req(base_url + "/file_not_found");
  http::client c;
  http::client::response r = c.get(req);

  BOOST_CHECK(body(r).size() != 0);
}

BOOST_AUTO_TEST_CASE(head_test) {
  using namespace boost::network;
  http::client::request request_(base_url + "/test.xml");
  http::client client_;
  http::client::response response_;
  BOOST_REQUIRE_NO_THROW(response_ = client_.head(request_));
  BOOST_REQUIRE(headers(response_).count("Content-Length") != 0);
  headers_range<http::client::response>::type range =
      headers(response_)["Content-Length"];
  BOOST_CHECK_EQUAL(boost::begin(range)->first, "Content-Length");
  BOOST_CHECK_EQUAL(boost::begin(range)->second, "113");
  BOOST_CHECK(body(response_).size() == 0);
}

BOOST_AUTO_TEST_CASE(post_with_explicit_headers) {
  // This test checks that the headers echoed through echo_headers.py
  // are in fact the same as what are sent through the POST request
  using namespace boost::network;

  const std::string postdata = "empty";
  const std::string content_length = get_content_length(postdata);
  const std::string content_type = "application/x-www-form-urlencoded";

  http::client::request req(base_url + "/cgi-bin/echo_headers.py");
  req << header("Content-Length", content_length);
  req << header("Content-Type", content_type);
  req << body(postdata);

  http::client c;
  http::client::response r;
  BOOST_REQUIRE_NO_THROW(r = c.post(req));

  std::map<std::string, std::string> headers = parse_headers(body(r));
  BOOST_CHECK_EQUAL(headers["content-length"], content_length);
  BOOST_CHECK_EQUAL(headers["content-type"], content_type);
}

BOOST_AUTO_TEST_CASE(post_with_implicit_headers) {
  // This test checks that post(request, body) derives Content-Length
  // and Content-Type
  using namespace boost::network;

  const std::string postdata = "empty";

  http::client::request req(base_url + "/cgi-bin/echo_headers.py");

  http::client c;
  http::client::response r;
  BOOST_REQUIRE_NO_THROW(r = c.post(req, postdata));

  std::map<std::string, std::string> headers = parse_headers(body(r));
  BOOST_CHECK_EQUAL(headers["content-length"], get_content_length(postdata));
  BOOST_CHECK_EQUAL(headers["content-type"], "x-application/octet-stream");
}

BOOST_AUTO_TEST_CASE(post_with_explicit_content_type) {
  // This test checks that post(request, content_type, body) derives
  // Content-Length,
  // and keeps Content-Type
  using namespace boost::network;

  const std::string postdata = "empty";
  const std::string content_type = "application/x-my-content-type";

  http::client::request req(base_url + "/cgi-bin/echo_headers.py");

  http::client c;
  http::client::response r;
  BOOST_REQUIRE_NO_THROW(r = c.post(req, content_type, postdata));

  std::map<std::string, std::string> headers = parse_headers(body(r));
  BOOST_CHECK_EQUAL(headers["content-length"], get_content_length(postdata));
  BOOST_CHECK_EQUAL(headers["content-type"], content_type);
}

BOOST_AUTO_TEST_CASE(post_body_default_content_type) {
  // This test checks that post(request, body) gets the post data
  // through to the server
  using namespace boost::network;

  const std::string postdata = "firstname=bill&lastname=badger";

  http::client::request req(base_url + "/cgi-bin/echo_body.py");

  http::client c;
  http::client::response r;
  BOOST_REQUIRE_NO_THROW(r = c.post(req, postdata));
  http::client::response::string_type body_ = body(r);
  BOOST_CHECK_EQUAL(postdata, body_);
}

BOOST_AUTO_TEST_CASE(post_with_custom_headers) {
  // This test checks that custom headers pass through to the server
  // when posting
  using namespace boost::network;

  http::client::request req(base_url + "/cgi-bin/echo_headers.py");
  req << header("X-Cpp-Netlib", "rocks!");

  http::client c;
  http::client::response r;
  BOOST_REQUIRE_NO_THROW(r = c.post(req, std::string()));

  std::map<std::string, std::string> headers = parse_headers(body(r));
  BOOST_CHECK_EQUAL(headers["x-cpp-netlib"], "rocks!");
}
