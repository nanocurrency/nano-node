#ifndef BOOST_NETWORK_UTILS_BASE64_STATELESS_HPP
#define BOOST_NETWORK_UTILS_BASE64_STATELESS_HPP

#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/range/begin.hpp>
#include <boost/range/end.hpp>
#include <algorithm>
#include <iterator>
#include <string>

namespace boost {
namespace network {
namespace utils {

// Uses base64_from_binary and transform_width to implement a BASE64
// converter working on an iterator range.  While it is a nice example
// of code reuse, the input sequence must either end at the three-byte
// boundary or be padded with a zero, otherwise the transforming
// iterator tries to read behind the end iterator.
//
// Summarized interface:
//
// OutputIterator encode(InputIterator begin, InputIterator end,
//                       OutputIterator output)
// OutputIterator encode(InputRange const & input, OutputIterator output)
// OutputIterator encode(char const * value, OutputIterator output)
// std::basic_string<Char> encode(InputRange const & value)
// std::basic_string<Char> encode(char const * value)

namespace base64_stateless {

// force using the ostream_iterator from boost::archive to write wide
// characters reliably, although wchar_t may not be a native character
// type
using namespace boost::archive::iterators;

template <typename InputIterator, typename OutputIterator>
OutputIterator encode(InputIterator begin, InputIterator end,
                      OutputIterator output) {
  // declare the encoding iterator type
  typedef base64_from_binary<transform_width<InputIterator, 6, 8> > base64_text;
  base64_text base64_begin(begin), base64_end(end);
  // iterate through the input by the encoding iterator one encoded
  // unit at a time to learn how many units were encoded without
  // requiring neither the input iterators nor the output ones to be
  // random access iterators (supporting subtraction end - begin)
  std::size_t encoded_count = 0;
  while (base64_begin != base64_end) {
    *output++ = *base64_begin++;
    ++encoded_count;
  }
  // the encoding iterator advanced so many times as is the encoded
  // output
  // size, but the padding is determined by the number of bytes in the
  // last
  // (incomplete) input byte-triplet; first compute the input length and
  // then how many trailing bytes followed the last complete quantum
  std::size_t incomplete_length = encoded_count * 6 / 8 % 3;
  if (incomplete_length > 0) {
    *output++ = '=';
    if (incomplete_length < 2) *output++ = '=';
  }
  return output;
}

template <typename InputRange, typename OutputIterator>
OutputIterator encode(InputRange const& input, OutputIterator output) {
  return encode(boost::begin(input), boost::end(input), output);
}

template <typename OutputIterator>
OutputIterator encode(char const* value, OutputIterator output) {
  return encode(value, value + strlen(value), output);
}

template <typename Char, typename InputRange>
std::basic_string<Char> encode(InputRange const& value) {
  std::basic_string<Char> result;
  encode(value, std::back_inserter(result));
  return result;
}

template <typename Char>
std::basic_string<Char> encode(char const* value) {
  std::basic_string<Char> result;
  encode(value, std::back_inserter(result));
  return result;
}

}  // namespace base64_stateless

}  // namespace utils
}  // namespace network
}  // namespace boost

#endif  // BOOST_NETWORK_UTILS_BASE64_STATELESS_HPP
