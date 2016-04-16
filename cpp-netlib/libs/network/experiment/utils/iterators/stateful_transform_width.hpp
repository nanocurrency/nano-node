#ifndef BOOST_NETWORK_UTILS_ITERATORS_STATEFUL_TRANSFORM_WIDTH_HPP
#define BOOST_NETWORK_UTILS_ITERATORS_STATEFUL_TRANSFORM_WIDTH_HPP

#include <boost/serialization/pfto.hpp>
#include <boost/iterator/iterator_adaptor.hpp>
#include <boost/iterator/iterator_traits.hpp>
#include <algorithm>

// The class stateful_transform_width works like transform_width from
// boost/archive/iterators if the collection of input values can be converted
// to the output without padding the input with zeros. (The total input bit
// count is divisible by the bit size of an output value.)  It it cannot, the
// partially encoded last value, which would have to be padded with zero,
// is stored to the state_for_transform_width, which can be used later to
// continue the encoding when another chunk of input values is available.
// The end iterator is needed to detect the end transformation.
//
// The encoding state and the end iterator are owned by the input iterator
// which has to provide access to them.  The iterator adaptor
// iterator_with_state is supposed to be used to add this functionality to
// existing iterators.

namespace boost {
namespace network {
namespace utils {
namespace iterators {

template <class Value, int BitsOut, int BitsIn>
class state_for_transform_width {
 public:
  typedef Value value_type;

  state_for_transform_width() : m_displacement(0) {}

  state_for_transform_width(state_for_transform_width const &source)
      : m_displacement(source.m_displacement), m_buffer(source.m_buffer) {}

  bool empty() const { return bit_count() == 0; }

  void clear() { m_displacement = 0; }

 protected:
  unsigned short bit_count() const {
    return m_displacement > 0 ? BitsIn - m_displacement : 0;
  }

 private:
  unsigned short m_displacement;
  value_type m_buffer;

  template <class Base, int BitsOut2, int BitsIn2, class Char>
  friend class stateful_transform_width;
};

template <
    class Base, int BitsOut, int BitsIn,
    class Char = typename boost::iterator_value<Base>::type  // output character
    >
class stateful_transform_width
    : public boost::iterator_adaptor<
          stateful_transform_width<Base, BitsOut, BitsIn, Char>, Base, Char,
          single_pass_traversal_tag, Char> {
  friend class boost::iterator_core_access;
  typedef typename boost::iterator_adaptor<
      stateful_transform_width<Base, BitsOut, BitsIn, Char>, Base, Char,
      single_pass_traversal_tag, Char> super_t;

  typedef stateful_transform_width<Base, BitsOut, BitsIn, Char> this_t;
  typedef typename iterator_value<Base>::type base_value_type;

  Char fill();

  Char dereference_impl() {
    if (!m_full) {
      m_current_value = fill();
      m_full = true;
    }
    return m_current_value;
  }

  Char dereference() const {
    return const_cast<this_t *>(this)->dereference_impl();
  }

  bool equal(const this_t &rhs) const {
    return this->base_reference() == rhs.base_reference();
  }

  void increment() {
    m_displacement += BitsOut;

    while (m_displacement >= BitsIn) {
      m_displacement -= BitsIn;
      if (0 == m_displacement) m_bufferfull = false;
      if (!m_bufferfull) ++this->base_reference();
    }

    // detect if the number of bits in the m_buffer is not enough
    // to encode a full 6-bit unit - read the next byte from the input
    if (m_displacement >= 0 && BitsIn - m_displacement < BitsOut) {
      // the following condition is compilable only with this variable
      typename this_t::base_type const &end_ = this->base_reference().end();
      if (this->base_reference() != end_) {
        // read the next byte from the input or make it zero to
        // provide padding to encode th elast byte
        m_next_buffer =
            ++this->base_reference() != end_ ? *this->base_reference() : 0;
        m_nextfull = true;
      }
      // store the encoding state if we encountered the last byte
      if (this->base_reference() == end_) {
        typename Base::state_type &state = this->base_reference().state();
        state.m_displacement = m_displacement;
        state.m_buffer = m_buffer;
      }
    }

    m_full = false;
  }

  // the most recent encoded character
  Char m_current_value;
  // number of bits left in current input character buffer
  unsigned int m_displacement;
  // value to be just encoded and a next value to be encoded
  base_value_type m_buffer, m_next_buffer;
  // flag to current output character is ready - just used to save time
  bool m_full;
  // flag to indicate that m_buffer and/or m_nextbuffer have data
  bool m_bufferfull, m_nextfull;

 public:
  template <class T>
  stateful_transform_width(BOOST_PFTO_WRAPPER(T) start)
      : super_t(Base(BOOST_MAKE_PFTO_WRAPPER(static_cast<T>(start)))),
        m_displacement(0),
        m_full(false),
        m_bufferfull(false),
        m_nextfull(false) {}

  stateful_transform_width(const stateful_transform_width &rhs)
      : super_t(rhs.base_reference()),
        m_current_value(rhs.m_current_value),
        m_displacement(rhs.m_displacement),
        m_buffer(rhs.m_buffer),
        m_next_buffer(rhs.m_next_buffer),
        m_full(rhs.m_full),
        m_bufferfull(rhs.m_bufferfull),
        m_nextfull(rhs.m_nextfull) {}
};

template <class Base, int BitsOut, int BitsIn, class Char>
Char stateful_transform_width<Base, BitsOut, BitsIn, Char>::fill() {
  typename Base::state_type &state = this->base_reference().state();
  if (!state.empty()) {
    // initialize the m_buffer from the state and put the current input
    // byte to the m_next_buffer to make it follow right after
    m_displacement = state.m_displacement;
    m_buffer = state.m_buffer;
    m_bufferfull = true;
    m_next_buffer = *this->base_reference();
    m_nextfull = true;
    state.clear();
  }
  Char retval = 0;
  unsigned int missing_bits = BitsOut;
  for (;;) {
    unsigned int bcount;
    if (!m_bufferfull) {
      // fill the current m_buffer firstly from m_next_buffer if
      // available then read the input sequence
      if (m_nextfull) {
        m_buffer = m_next_buffer;
        m_nextfull = false;
      } else {
        m_buffer = *this->base_reference();
      }
      m_bufferfull = true;
      bcount = BitsIn;
    } else
      bcount = BitsIn - m_displacement;
    unsigned int i = (std::min)(bcount, missing_bits);
    // shift interesting bits to least significant position
    unsigned int j = m_buffer >> (bcount - i);
    // strip off uninteresting bits
    // (note presumption of two's complement arithmetic)
    j &= ~(-(1 << i));
    // append then interesting bits to the output value
    retval <<= i;
    retval |= j;
    missing_bits -= i;
    if (0 == missing_bits) break;
    // if we used a byte from the input sequence and not from the
    // prepared m_next_buffer, advance the input sequence iterator
    if (!m_nextfull) ++this->base_reference();
    m_bufferfull = false;
  }
  return retval;
}

}  // namespace iterators
}  // namespace utils
}  // namespace network
}  // namespace boost

#endif  // BOOST_NETWORK_UTILS_ITERATORS_STATEFUL_TRANSFORM_WIDTH_HPP
