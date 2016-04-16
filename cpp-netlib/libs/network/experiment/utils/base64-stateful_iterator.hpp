#ifndef BOOST_NETWORK_UTILS_BASE64_STATEFUL_ITERATOR_HPP
#define BOOST_NETWORK_UTILS_BASE64_STATEFUL_ITERATOR_HPP

#include <boost/archive/iterators/base64_from_binary.hpp>
#include "iterators/stateful_transform_width.hpp"
#include "iterators/iterator_with_state.hpp"
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

// Uses base64_from_binary and stateful_transform_width to implement a
// BASE64
// converter working on an iterator range.  Based on transform_width, the
// stateful_transform_width relies on the end iterator and the encoding
// state
// stored in the transformed iterator; usually by using the iterator
// adaptor
// - iterator_with_state.  Storing the encoding state in the transformed
// iterator practically means adapting every such iterator, but the
// iterators
// which wrap the stateful_transform_width, like the base64_from_binary,
// do
// not need to be re-declared with an extra constructor to pass the end
// iterator and the encoding state to an extended transform_width.
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

namespace base64_stateful_iterator {

// force using the ostream_iterator from boost::archive to write wide
// characters reliably, althoth wchar_t may not be a native character
// type
using namespace boost::archive::iterators;
using namespace boost::network::utils::iterators;

template <typename Value>
struct state : public state_for_transform_width<Value, 6, 8> {
  typedef state<Value> this_t;
  typedef state_for_transform_width<Value, 6, 8> super_t;

  state() {}
  state(this_t const& source) : super_t(source) {}

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
  typedef boost::iterator_with_state<InputIterator, State> stateful_input;
  // declare the encoding iterator type
  typedef base64_from_binary<stateful_transform_width<stateful_input, 6, 8> >
      base64_input;
  // declare the stateful transforming itarators
  stateful_input stateful_begin(begin, end, rest), stateful_end(end);
  // declare the iterator to transform the encoded 6-bit units to the
  // BASE64 alphabet
  base64_input base64_begin(stateful_begin), base64_end(stateful_end);
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
    typedef boost::iterator_with_state<typename pillow_input::const_iterator,
                                       State> stateful_input;
    // declare the encoding iterator type
    typedef base64_from_binary<stateful_transform_width<stateful_input, 6, 8> >
        base64_input;
    // although containing encoding state to continue with the next
    // chunk, the stateful_transform_width still reads from the input
    // iterator and needs the complete quantum for the encoding; the
    // zero padding will make for an artifitial input ending here
    pillow_input pillow = {{0, 0}};
    stateful_input stateful_begin(pillow.begin(), pillow.end(), rest);
    base64_input base64_begin(stateful_begin);
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

}  // namespace base64_stateful_iterator

}  // namespace utils
}  // namespace network
}  // namespace boost

#endif  // BOOST_NETWORK_UTILS_BASE64_STATEFUL_ITERATOR_HPP
