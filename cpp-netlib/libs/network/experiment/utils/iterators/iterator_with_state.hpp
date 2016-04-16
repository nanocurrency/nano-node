#ifndef BOOST_ITERATOR_WITH_STATE_HPP
#define BOOST_ITERATOR_WITH_STATE_HPP

#include <boost/iterator.hpp>
#include <boost/iterator/iterator_adaptor.hpp>
#include <boost/iterator/iterator_categories.hpp>
#include <boost/type_traits/is_class.hpp>
#include <boost/static_assert.hpp>

// The class iterator_with_state adds storing the end iterator and the
// transforming state to an existing iterator, so that it can be used as
// a base in the transform_width_with_state.

namespace boost {
template <class Iterator, class State>
class iterator_with_state;

namespace detail {
template <class Iterator, class State>
struct iterator_with_state_base {
  typedef iterator_adaptor<
      iterator_with_state<Iterator, State>, Iterator, use_default,
      typename mpl::if_<
          is_convertible<typename iterator_traversal<Iterator>::type,
                         random_access_traversal_tag>,
          bidirectional_traversal_tag, use_default>::type> type;
};
}

template <class Iterator, class State>
class iterator_with_state
    : public detail::iterator_with_state_base<Iterator, State>::type {
  typedef typename detail::iterator_with_state_base<Iterator, State>::type
      super_t;

  friend class iterator_core_access;

 public:
  iterator_with_state() {}

  typedef State state_type;

  iterator_with_state(Iterator x, Iterator end_, State& state_)
      : super_t(x), m_end(end_), m_state(&state_) {}

  iterator_with_state(Iterator x) : super_t(x) {}

  template <class OtherIterator>
  iterator_with_state(
      iterator_with_state<OtherIterator, State> const& t,
      typename enable_if_convertible<OtherIterator, Iterator>::type* = 0)
      : super_t(t.base()), m_end(t.end()), m_state(&t.state()) {}

  Iterator const& end() const { return m_end; }

  State const& state() const { return *m_state; }

  State& state() { return *m_state; }

 private:
  void increment() { ++this->base_reference(); }

  void decrement() { --(this->base_reference()); }

  Iterator m_end;
  State* m_state;
};

template <class Iterator, class State>
iterator_with_state<Iterator, State> make_iterator_with_state(
    Iterator x, Iterator end = Iterator(), State state = State()) {
  return iterator_with_state<Iterator, State>(x, end, state);
}

}  // namespace boost

#endif  // BOOST_ITERATOR_WITH_STATE_HPP
