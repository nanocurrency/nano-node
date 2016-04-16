
//          Copyright Dean Michael Berris 2007.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE message test
#include <boost/config/warning_disable.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/network/include/message.hpp>
#include <algorithm>

BOOST_AUTO_TEST_CASE(message_transform_toupper) {
  using namespace boost::network;

  message msg;
  msg << source("me");
  BOOST_CHECK_EQUAL(source(msg), "me");
  msg << transform(to_upper_, source_);
  BOOST_CHECK_EQUAL(source(msg), "ME");
  msg << destination("you");
  BOOST_CHECK_EQUAL(destination(msg), "you");
  msg << transform(to_upper_, destination_);
  BOOST_CHECK_EQUAL(destination(msg), "YOU");
}

BOOST_AUTO_TEST_CASE(message_transform_tolower) {
  using namespace boost::network;

  message msg;
  msg << source("ME");
  BOOST_CHECK_EQUAL(source(msg), "ME");
  msg << transform(to_lower_, source_);
  BOOST_CHECK_EQUAL(source(msg), "me");
  msg << destination("YOU");
  BOOST_CHECK_EQUAL(destination(msg), "YOU");
  msg << transform(to_lower_, destination_);
  BOOST_CHECK_EQUAL(destination(msg), "you");
}
