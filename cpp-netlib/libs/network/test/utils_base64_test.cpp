#define BOOST_TEST_MODULE BASE64 Test
#include <boost/config/warning_disable.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/network/utils/base64/encode.hpp>
#include <boost/network/utils/base64/encode-io.hpp>
#include <boost/array.hpp>
#include <algorithm>
#include <iterator>
#include <string>
#include <vector>
#include <sstream>

using namespace boost::network::utils;

// proves that all public functions are compilable; the result check
// is very minimum here, so that the test doesn't look so stupid ;-)
BOOST_AUTO_TEST_CASE(interface_test) {
  std::string result;
  base64::state<char> state;

  // check string literal
  BOOST_CHECK_EQUAL(base64::encode<char>("abc"), "YWJj");

  base64::encode("abc", std::back_inserter(result));
  BOOST_CHECK_EQUAL(result, "YWJj");

  result.clear();
  base64::encode("abc", std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "YWJj");

  // check std::string
  std::string input("abc");

  BOOST_CHECK_EQUAL(base64::encode<char>(input), "YWJj");

  result.clear();
  base64::encode(input, std::back_inserter(result));
  BOOST_CHECK_EQUAL(result, "YWJj");

  result.clear();
  base64::encode(input.begin(), input.end(), std::back_inserter(result));
  BOOST_CHECK_EQUAL(result, "YWJj");

  result.clear();
  base64::encode(input, std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "YWJj");

  result.clear();
  base64::encode(input.begin(), input.end(), std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "YWJj");

  // check array of chars
  char char_array[] = {'a', 'b', 'c', 0};

  BOOST_CHECK_EQUAL(base64::encode<char>(char_array), "YWJj");

  // check boost::array of chars
  boost::array<char, 3> char_boost_array = {{'a', 'b', 'c'}};

  BOOST_CHECK_EQUAL(base64::encode<char>(char_boost_array), "YWJj");

  // check std::vector of chars
  std::vector<char> char_vector(char_array, char_array + 3);

  BOOST_CHECK_EQUAL(base64::encode<char>(char_vector), "YWJj");

  // check array of ints
  int int_array[] = {'a', 'b', 'c'};

  BOOST_CHECK_EQUAL(base64::encode<char>(int_array), "YWJj");

  // check boost::array of ints
  boost::array<int, 3> int_boost_array = {{'a', 'b', 'c'}};

  BOOST_CHECK_EQUAL(base64::encode<char>(int_boost_array), "YWJj");

  // check std::vector of ints
  std::vector<int> int_vector(int_array, int_array + 3);

  BOOST_CHECK_EQUAL(base64::encode<char>(int_vector), "YWJj");

  // check that base64::encode_rest is compilable and callable
  result.clear();
  base64::encode_rest(std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "");

  // check that the iostream interface is compilable and callable
  std::ostringstream output;
  output << base64::io::encode("abc")
         << base64::io::encode(input.begin(), input.end())
         << base64::io::encode(int_array) << base64::io::encode(int_boost_array)
         << base64::io::encode(char_array)
         << base64::io::encode(char_boost_array)
         << base64::io::encode(char_vector) << base64::io::encode_rest<char>;
  BOOST_CHECK_EQUAL(output.str(), "YWJjYWJjYWJjYWJjYWJjYWJjYWJj");
}

// checks that functions encoding a single chunk append the correct padding
// if the input byte count is not divisible by 3
BOOST_AUTO_TEST_CASE(padding_test) {
  BOOST_CHECK_EQUAL(base64::encode<char>(""), "");
  BOOST_CHECK_EQUAL(base64::encode<char>("a"), "YQ==");
  BOOST_CHECK_EQUAL(base64::encode<char>("aa"), "YWE=");
  BOOST_CHECK_EQUAL(base64::encode<char>("aaa"), "YWFh");
}

// check that functions using encoding state interrupt and resume encoding
// correcly if the byte count of the partial input is not divisible by 3
BOOST_AUTO_TEST_CASE(state_test) {
  base64::state<char> state;
  std::string result;

  // check encoding empty input; including the state value
  base64::encode("", std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "");
  BOOST_CHECK(state.empty());
  result.clear();
  state.clear();

  // check one third of quantum which needs two character padding;
  // including how the state develops when encoded by single character
  base64::encode("a", std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "Y");
  BOOST_CHECK(!state.empty());
  base64::encode_rest(std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "YQ==");
  BOOST_CHECK(state.empty());
  result.clear();
  state.clear();

  // check two thirds of quantum which needs one character padding;
  // including how the state develops when encoded by single character
  base64::encode("a", std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "Y");
  BOOST_CHECK(!state.empty());
  base64::encode("a", std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "YW");
  BOOST_CHECK(!state.empty());
  base64::encode_rest(std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "YWE=");
  BOOST_CHECK(state.empty());
  result.clear();
  state.clear();

  // check a complete quantum which needs no padding; including
  // how the state develops when encoded by single character
  base64::encode("a", std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "Y");
  BOOST_CHECK(!state.empty());
  base64::encode("a", std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "YW");
  BOOST_CHECK(!state.empty());
  base64::encode("a", std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "YWFh");
  BOOST_CHECK(state.empty());
  base64::encode_rest(std::back_inserter(result), state);
  BOOST_CHECK_EQUAL(result, "YWFh");
  BOOST_CHECK(state.empty());
}

// checks that the base64 output can be returned as wchar_t too
BOOST_AUTO_TEST_CASE(wide_character_test) {
  BOOST_CHECK(base64::encode<wchar_t>("abc") == L"YWJj");
  BOOST_CHECK(base64::encode<wchar_t>(std::string("abc")) == L"YWJj");

  std::wostringstream output;
  output << base64::io::encode("abc") << base64::io::encode_rest<char>;
  BOOST_CHECK(output.str() == L"YWJj");
}

// checks that the base64-io manipulators are compilable and work
BOOST_AUTO_TEST_CASE(io_test) {
  // check complete quantum where no state has to be remembered
  std::ostringstream output;
  output << base64::io::encode("abc") << base64::io::encode_rest<char>;
  BOOST_CHECK_EQUAL(output.str(), "YWJj");

  // check that encode_rest clears the state
  output.str("");
  output << base64::io::encode("a");
  BOOST_CHECK(!base64::io::empty_state<char>(output));
  output << base64::io::encode_rest<char>;
  BOOST_CHECK(base64::io::empty_state<char>(output));

  // check that forced clearing the state works
  output.str("");
  output << base64::io::encode("a");
  BOOST_CHECK(!base64::io::empty_state<char>(output));
  output << base64::io::clear_state<char>;
  BOOST_CHECK(base64::io::empty_state<char>(output));

  // check one third of quantum which has to be remembered in state
  output.str("");
  output << base64::io::encode("a") << base64::io::encode("bc")
         << base64::io::encode_rest<char>;
  BOOST_CHECK_EQUAL(output.str(), "YWJj");

  // check two thirds of quantum which have to be remembered in state.
  output.str("");
  output << base64::io::encode("ab") << base64::io::encode("c")
         << base64::io::encode_rest<char>;
  BOOST_CHECK_EQUAL(output.str(), "YWJj");
}
