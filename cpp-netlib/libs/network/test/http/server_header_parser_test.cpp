// Copyright 2013 Rudolfs Bundulis
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE HTTP Server Header Parser Test
#include <boost/network/protocol/http/server.hpp>
#include <boost/config/warning_disable.hpp>
#include <boost/test/unit_test.hpp>
#define BOOST_LOCALE_NO_LIB
#include <boost/locale/encoding.hpp>
#include <string>
#include <iostream>

/** Synopsis
*
*  Test for Utf8 support in the asynchronous connection header parser
*  --------------------------------------------
*
*  This test checks for Utf8 support in the header parser
*  for asynchronous connection
*
*/

namespace tags = boost::network::tags;
namespace logic = boost::logic;
namespace fusion = boost::fusion;
using namespace boost::network::http;

BOOST_AUTO_TEST_CASE(async_connection_parse_headers) {
  std::wstring utf16_test_name = L"R\u016bdolfs";
  request_header_narrow utf8_header = {
      "X-Utf8-Test-Header",
      boost::locale::conv::utf_to_utf<char>(utf16_test_name)};
  std::string valid_http_request;
  valid_http_request.append(utf8_header.name)
      .append(": ")
      .append(utf8_header.value)
      .append("\r\n\r\n");
  std::vector<request_header_narrow> headers;
  parse_headers(valid_http_request, headers);
  std::vector<request_header_narrow>::iterator header_iterator =
      headers.begin();
  for (; header_iterator != headers.end(); ++header_iterator) {
    if (header_iterator->name == utf8_header.name &&
        header_iterator->value == utf8_header.value)
      break;
  }
  std::wstring utf16_test_name_from_header =
      boost::locale::conv::utf_to_utf<wchar_t>(header_iterator->value);
  BOOST_CHECK(header_iterator != headers.end());
  BOOST_CHECK(utf16_test_name_from_header == utf16_test_name);
  std::cout << "utf8 header parsed, name: " << header_iterator->name
            << ", value: " << header_iterator->value;
}
