#ifndef BOOST_NETWORK_UTILS_BASE64_STATEFUL_TRANSFORM_HPP
#define BOOST_NETWORK_UTILS_BASE64_STATEFUL_TRANSFORM_HPP

#include "iterators/stateful_base64_from_binary.hpp"
#include "iterators/transform_width_with_state.hpp"
#include <boost/range/begin.hpp>
#include <boost/range/end.hpp>
#include <boost/array.hpp>
#include <algorithm>
#include <iterator>
#include <sstream>
#include <string>

namespace boost {
namespace network {
namespace utils {

// Uses stateful_base64_from_binary and transform_width_with_state to
// implement a BASE64 converter working on an iterator range.  Based on
// transform_width, the transform_width_with_state stores the end iterator
// and the encoding state to be able to process the encoding input by
// chunks.  The stateful_base64_from_binary just passes through the
// constructor parameters, which the transform_width_with_state needs.
// Storing the encoding state in the transforming iterator makes any
// input iterator transformable without adapting, but the iterators which
// wrap it, like the base64_from_binary, needs to be re-declared with
// the extra constructor.
//
// Summarized interface:
//
// struct state<Value>  {
//     bool empty () const;
//     void clear();
//     unsigned short padding_length() const;
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

namespace base64_stateful_transform {

// force using the ostream_iterator from boost::archive to write wide
// characters reliably, althoth wchar_t may not be a native character
// type
using namespace boost::archive::iterators;
using namespace boost::network::utils::iterators;

template <typename Value>
struct state : public transform_width_state<Value, 6, 8> {
  typedef transform_width_state<Value, 6, 8> super_t;

  state() {}
  state(state<Value> const& source) : super_t(source) {}

  unsigned short padding_length() const {
    // the BASE64 encoding unit consists of 6 bits; if the 8-bit
    // input cannot be completely divided to 6-bit chunks, 2 or
    // 4 bits can remain, which the bit_count returns as displacement
    // of the 8-bit value (right shift length) - 6 or 4
    unsigned short bits = super_t::bit_count();
    return bits > 0 ? 6 / bits : 0;
  }
};

template <typename InputIterator, typename OutputIterator, typename State>
OutputIterator encode(InputIterator begin, InputIterator end,
                      OutputIterator output, State& rest) {
  // declare the transforming iterator type
  typedef transform_width_with_state<InputIterator, 6, 8> stateful_input;
  // declare the encoding iterator type
  typedef stateful_base64_from_binary<stateful_input> base64_input;
  base64_input base64_begin(begin, end, rest), base64_end(end);
  return std::copy(base64_begin, base64_end, output);
}

template <typename State, typename OutputIterator>
OutputIterator encode_rest(OutputIterator output, State& rest) {
  unsigned short padding_length = rest.padding_length();
  if (padding_length > 0) {
    typedef typename State::value_type value_type;
    // declare the input padding type and the adapted iterator including
    // the encoding state for it - see below
    typedef boost::array<value_type, 2> pillow_input;
    // declare the transforming iterator type
    typedef transform_width_with_state<typename pillow_input::const_iterator, 6,
                                       8> stateful_input;
    // declare the encoding iterator type
    typedef stateful_base64_from_binary<stateful_input> base64_input;
    // although containing encoding state to continue with the next
    // chunk, the stateful_transform_width still reads from the input
    // iterator and needs the complete quantum for the encoding; the
    // zero padding will make for an artifitial input ending here
    pillow_input pillow = {{0, 0}};
    base64_input base64_begin(pillow.begin(), pillow.end(), rest),
        base64_end(pillow.end());
    *output++ = *base64_begin;
    if (padding_length > 0) {
      *output++ = '=';
      if (padding_length > 1) *output++ = '=';
    }
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

template <typename InputRange, typename OutputIterator, typename State>
OutputIterator encode(InputRange const& input, OutputIterator output,
                      State& rest) {
  return encode(boost::begin(input), boost::end(input), output, rest);
}

template <typename OutputIterator>
OutputIterator encode(char const* value, OutputIterator output,
                      state<char>& rest) {
  return encode(value, value + strlen(value), output, rest);
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

}  // namespace base64_stateful_transform

}  // namespace utils
}  // namespace network
}  // namespace boost

#endif  // BOOST_NETWORK_UTILS_BASE64_STATEFUL_TRANSFORM_HPP
