#ifndef BOOST_NETWORK_UTILS_ITERATORS_STATEFUL_BASE64_FROM_BINARY_HPP
#define BOOST_NETWORK_UTILS_ITERATORS_STATEFUL_BASE64_FROM_BINARY_HPP

#include <boost/serialization/pfto.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/archive/iterators/dataflow_exception.hpp>
#include <boost/assert.hpp>
#include <cstddef>

// The class stateful_base64_from_binary works like base64_from_binary from
// boost/archive/iterators, only expecting an iterator base which supports
// transformng state and needs the end iterator and the state passed to the
// constructor.  The transform_width_with_state, for example.

namespace boost {
namespace network {
namespace utils {
namespace iterators {

namespace detail {

template <class CharType>
struct from_6_bit {
  typedef CharType result_type;
  CharType operator()(CharType t) const {
    const char* lookup_table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        "+/";
    BOOST_ASSERT(t < 64);
    return lookup_table[static_cast<std::size_t>(t)];
  }
};

}  // namespace detail

template <class Base,
          class CharType = typename boost::iterator_value<Base>::type>
class stateful_base64_from_binary
    : public transform_iterator<detail::from_6_bit<CharType>, Base> {
  friend class boost::iterator_core_access;
  typedef transform_iterator<typename detail::from_6_bit<CharType>, Base>
      super_t;

 public:
  template <class T, class State>
  stateful_base64_from_binary(BOOST_PFTO_WRAPPER(T) start,
                              BOOST_PFTO_WRAPPER(T) end, State& state)
      : super_t(
            Base(BOOST_MAKE_PFTO_WRAPPER(static_cast<T>(start)), end, state),
            detail::from_6_bit<CharType>()) {}
  template <class T>
  stateful_base64_from_binary(BOOST_PFTO_WRAPPER(T) start)
      : super_t(Base(BOOST_MAKE_PFTO_WRAPPER(static_cast<T>(start))),
                detail::from_6_bit<CharType>()) {}
  // intel 7.1 doesn't like default copy constructor
  stateful_base64_from_binary(const stateful_base64_from_binary& rhs)
      : super_t(Base(rhs.base_reference()), detail::from_6_bit<CharType>()) {}
};

}  // namespace iterators
}  // namespace utils
}  // namespace network
}  // namespace boost

#endif  // BOOST_NETWORK_UTILS_ITERATORS_STATEFUL_BASE64_FROM_BINARY_HPP
