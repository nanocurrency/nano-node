//            Copyright (c) Glyn Matthews 2011.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE URL encoding test
#include <boost/config/warning_disable.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/network/uri/encode.hpp>
#include <boost/network/uri/decode.hpp>
#include <iterator>

using namespace boost::network;

BOOST_AUTO_TEST_CASE(encoding_test) {
  const std::string unencoded(" !\"#$%&\'()*");
  const std::string encoded("%20%21%22%23%24%25%26%27%28%29%2A");

  std::string instance;
  uri::encode(unencoded, std::back_inserter(instance));
  BOOST_CHECK_EQUAL(instance, encoded);
}

BOOST_AUTO_TEST_CASE(decoding_test) {
  const std::string unencoded(" !\"#$%&\'()*");
  const std::string encoded("%20%21%22%23%24%25%26%27%28%29%2A");

  std::string instance;
  uri::decode(encoded, std::back_inserter(instance));
  BOOST_CHECK_EQUAL(instance, unencoded);
}

BOOST_AUTO_TEST_CASE(encoding_multibyte_test) {
  const std::string unencoded("한글 테스트");
  const std::string encoded("%ED%95%9C%EA%B8%80%20%ED%85%8C%EC%8A%A4%ED%8A%B8");

  std::string instance;
  uri::encode(unencoded, std::back_inserter(instance));
  BOOST_CHECK_EQUAL(instance, encoded);
}

BOOST_AUTO_TEST_CASE(decoding_multibyte_test) {
  const std::string unencoded("한글 테스트");
  const std::string encoded("%ED%95%9C%EA%B8%80%20%ED%85%8C%EC%8A%A4%ED%8A%B8");

  std::string instance;
  uri::decode(encoded, std::back_inserter(instance));
  BOOST_CHECK_EQUAL(instance, unencoded);
}
