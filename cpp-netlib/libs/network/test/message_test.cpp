
//          Copyright Dean Michael Berris 2007.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE message test
#include <boost/config/warning_disable.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/network.hpp>
#include <algorithm>
#include <boost/mpl/list.hpp>

using namespace boost::network;

typedef boost::mpl::list<http::tags::http_default_8bit_tcp_resolve,
                         http::tags::http_default_8bit_udp_resolve,
                         http::tags::http_keepalive_8bit_tcp_resolve,
                         http::tags::http_keepalive_8bit_udp_resolve,
                         tags::default_string, tags::default_wstring> tag_types;

struct string_header_name {
  static std::string string;
};

std::string string_header_name::string = "Header";

struct wstring_header_name {
  static std::wstring string;
};

std::wstring wstring_header_name::string = L"Header";

struct string_header_value {
  static std::string string;
};

std::string string_header_value::string = "Value";

struct wstring_header_value {
  static std::wstring string;
};

std::wstring wstring_header_value::string = L"Value";

template <class Tag>
struct header_name : string_header_name {};

template <>
struct header_name<tags::default_wstring> : wstring_header_name {};

template <class Tag>
struct header_value : string_header_value {};

template <>
struct header_value<tags::default_wstring> : wstring_header_value {};

struct string_body_data {
  static std::string string;
};

std::string string_body_data::string =
    "The quick brown fox jumps over the lazy dog.";

struct wstring_body_data {
  static std::wstring string;
};

std::wstring wstring_body_data::string =
    L"The quick brown fox jumps over the lazy dog.";

template <class Tag>
struct body_data : string_body_data {};

template <>
struct body_data<tags::default_wstring> : wstring_body_data {};

struct string_source_data {
  static std::string string;
};

std::string string_source_data::string = "Source";

struct wstring_source_data {
  static std::wstring string;
};

std::wstring wstring_source_data::string = L"Source";

template <class Tag>
struct source_data : string_source_data {};

template <>
struct source_data<tags::default_wstring> : wstring_body_data {};

struct string_destination_data {
  static std::string string;
};

std::string string_destination_data::string = "Destination";

struct wstring_destination_data {
  static std::wstring string;
};

std::wstring wstring_destination_data::string = L"Destination";

template <class Tag>
struct destination_data : string_destination_data {};

template <>
struct destination_data<tags::default_wstring> : wstring_destination_data {};

/**
 * Defines a set of template functions that can be used to test
 * generic code.
 */

BOOST_AUTO_TEST_CASE_TEMPLATE(copy_constructor_test, T, tag_types) {
  basic_message<T> instance;
  instance << header(header_name<T>::string, header_value<T>::string);
  basic_message<T> copy(instance);
  BOOST_CHECK_EQUAL(headers(copy).count(header_name<T>::string),
                    static_cast<std::size_t>(1));
  typename headers_range<basic_message<T> >::type range =
      headers(copy)[header_name<T>::string];
  BOOST_CHECK(boost::begin(range) != boost::end(range));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(swap_test, T, tag_types) {
  basic_message<T> instance;
  instance << header(header_name<T>::string, header_value<T>::string);
  basic_message<T> other;
  swap(instance, other);
  BOOST_CHECK_EQUAL(headers(instance).count(header_name<T>::string),
                    static_cast<std::size_t>(0));
  BOOST_CHECK_EQUAL(headers(other).count(header_name<T>::string),
                    static_cast<std::size_t>(1));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(headers_directive_test, T, tag_types) {
  basic_message<T> instance;
  instance << header(header_name<T>::string, header_value<T>::string);
  BOOST_CHECK_EQUAL(headers(instance).count(header_name<T>::string),
                    static_cast<std::size_t>(1));
  typename headers_range<basic_message<T> >::type range =
      headers(instance)[header_name<T>::string];
  BOOST_CHECK(boost::begin(range) != boost::end(range));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(body_directive_test, T, tag_types) {
  basic_message<T> instance;
  instance << ::boost::network::body(body_data<T>::string);
  typename string<T>::type body_string = body(instance);
  BOOST_CHECK(body_string == body_data<T>::string);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(source_directive_test, T, tag_types) {
  basic_message<T> instance;
  instance << ::boost::network::source(source_data<T>::string);
  typename string<T>::type source_string = source(instance);
  BOOST_CHECK(source_string == source_data<T>::string);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(destination_directive_test, T, tag_types) {
  basic_message<T> instance;
  instance << destination(destination_data<T>::string);
  BOOST_CHECK(destination(instance) == destination_data<T>::string);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(remove_header_directive_test, T, tag_types) {
  basic_message<T> instance;
  instance << header(header_name<T>::string, header_value<T>::string)
           << remove_header(header_name<T>::string);
  typename headers_range<basic_message<T> >::type range = headers(instance);
  BOOST_CHECK(boost::begin(range) == boost::end(range));
}
