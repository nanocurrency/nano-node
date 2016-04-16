#ifndef BOOST_NETWORK_UTILS_BASE64_STATEFUL_BUFFER_HPP
#define BOOST_NETWORK_UTILS_BASE64_STATEFUL_BUFFER_HPP

#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/range/begin.hpp>
#include <boost/range/end.hpp>
#include <boost/array.hpp>
#include <algorithm>
#include <iterator>
#include <string>

namespace boost {
namespace network {
namespace utils {

// Uses base64_from_binary and transform_width to implement a BASE64
// converter working on an iterator range.  Because the transform_width
// encodes immediately every input byte, while the BASE64 encoding
// processes
// the input by byte-triplets, if the input sequence does not end at the
// three-byte boundary, the rest is remembered in an encoding state to
// be able to continue with the next chunk.  It uses an internal buffer
// of 4095 input octets to be able to read the input by octet-triplets.
//
// Summarized interface:
//
// struct state<Value>  {
//     bool empty () const;
//     void clear();
// }
//
// OutputIterator encode(InputIterator begin, InputIterator end,
//                       OutputIterator output, State & rest)
// OutputIterator encode_rest(OutputIterator output, State & rest)
// OutputIterator encode(InputRange const & input, OutputIterator output,
//                       State & rest)
// OutputIterator encode(char const * value, OutputIterator output,
//                       state<char> & rest)
// std::basic_string<Char> encode(InputRange const & value, State & rest)
// std::basic_string<Char> encode(char const * value, state<char> & rest)
//
// OutputIterator encode(InputIterator begin, InputIterator end,
//                       OutputIterator output)
// OutputIterator encode(InputRange const & input, OutputIterator output)
// OutputIterator encode(char const * value, OutputIterator output)
// std::basic_string<Char> encode(InputRange const & value)
// std::basic_string<Char> encode(char const * value) {

namespace base64_stateful_buffer {

// force using the ostream_iterator from boost::archive to write wide
// characters reliably, althoth wchar_t may not be a native character
// type
using namespace boost::archive::iterators;

template <typename Value>
struct state {
  state() : size(0) {}

  state(state<Value> const& source) : data(source.data), size(source.size) {}

  bool empty() const { return size == 0; }

  void clear() { size = 0; }

 private:
  typedef boost::array<Value, 3> data_type;
  typedef typename data_type::const_iterator const_iterator_type;

  template <typename InputIterator>
  void fill(InputIterator begin, InputIterator end) {
    // make sure that there is always zero padding for the incomplete
    // triplet; the encode will read three bytes from the vector
    data.fill(0);
    size = std::copy(begin, end, data.begin()) - data.begin();
  }

  template <typename OutputIterator>
  OutputIterator write(OutputIterator output) {
    return std::copy(data.begin(), data.begin() + size, output);
  }

  const_iterator_type begin() const { return data.begin(); }

  const_iterator_type end() const { return data.begin() + size; }

  data_type data;
  std::size_t size;

  template <typename InputIterator, typename OutputIterator, typename State>
  friend OutputIterator encode(InputIterator begin, InputIterator end,
                               OutputIterator output, State& rest);
  template <typename State, typename OutputIterator>
  friend OutputIterator encode_rest(OutputIterator output, State& rest);
};

template <typename InputIterator, typename OutputIterator, typename State>
OutputIterator encode(InputIterator begin, InputIterator end,
                      OutputIterator output, State& rest) {
  typedef typename iterator_value<InputIterator>::type value_type;
  // declare the buffer type for 1365 octet triplets; make sure that the
  // number is divisible by three if you change it (!)
  const std::size_t BufferSize = 4095;
  BOOST_STATIC_ASSERT(BufferSize / 3 * 3 == BufferSize);
  typedef boost::array<value_type, BufferSize> buffer_type;
  // declare the encoding iterator type
  typedef base64_from_binary<transform_width<InputIterator, 6, 8> > base64_text;
  if (begin != end) {
    // declare the buffer, a variable to remmeber its size and the size
    // which can be encoded (the nearest lower size divisible by three)
    buffer_type buffer;
    std::size_t buffer_size = 0, encode_size = 0;
    // if the previous state contained an incomplete octet triplet, put
    // it to the start of the buffer to get it prepended to the input
    if (!rest.empty()) {
      buffer_size = rest.size;
      rest.write(buffer.begin());
      rest.clear();
    }
    // iterate over the entire input
    while (begin != end) {
      // fill the buffer with the input as much as possible
      while (begin != end && buffer_size < buffer.size())
        buffer[buffer_size++] = *begin++;
      // if the buffer could not be filled completely, compute
      // the size which can be encoded immediately.
      encode_size = buffer_size / 3 * 3;
      if (encode_size > 0) {
        // encode the buffer part of the size divisible by three
        base64_text base64_begin(buffer.begin()),
            base64_end(buffer.begin() + encode_size);
        output = std::copy(base64_begin, base64_end, output);
        // zero the buffer size to prepare for the next iteration
        buffer_size = 0;
      }
    }
    // if the complete buffer could not be encoded, store the last
    // incomplete octet triplet to the transiting state
    if (buffer_size > encode_size)
      rest.fill(buffer.begin() + encode_size, buffer.begin() + buffer_size);
  }
  return output;
}

template <typename State, typename OutputIterator>
OutputIterator encode_rest(OutputIterator output, State& rest) {
  typedef typename State::const_iterator_type iterator_type;
  // declare the encoding iterator type
  typedef base64_from_binary<transform_width<iterator_type, 6, 8> > base64_text;
  if (!rest.empty()) {
    // encode the incomplete octet triplet using zeros as padding
    // (an artificial input continuation)
    base64_text base64_begin(rest.begin()), base64_end(rest.end());
    output = std::copy(base64_begin, base64_end, output);
    // at least one padding '=' will be always needed - at least two
    // bits are missing in the finally encoded 6-bit value
    if (rest.size > 0) {
      *output++ = '=';
      // if the last octet was the first in the triplet (the index was,
      // four bits are missing in the finally encoded 6-bit value;
      // another '=' character is needed for the another two bits
      if (rest.size == 1) *output++ = '=';
    }
    rest.clear();
  }
  return output;
}

template <typename InputIterator, typename OutputIterator>
OutputIterator encode(InputIterator begin, InputIterator end,
                      OutputIterator output) {
  state<typename iterator_value<InputIterator>::type> rest;
  output = encode(begin, end, output, rest);
  return encode_rest(output, rest);
}

template <typename InputRange, typename OutputIterator>
OutputIterator encode(InputRange const& value, OutputIterator output) {
  return encode(boost::begin(value), boost::end(value), output);
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

// the function overloads for string literals encode the input without
// the terminating zero, which is usually expected, because the trailing
// zero byte is not considered a part of the string value; the overloads
// foran input range would wrap the string literal by Boost.Range and
// encodethe full memory occupated by the string literal - including the
// unwanted last zero byte

}  // namespace base64_stateful_buffer

}  // namespace utils
}  // namespace network
}  // namespace boost

#endif  // BOOST_NETWORK_UTILS_BASE64_STATEFUL_BUFFER_HPP
