//          Copyright Dean Michael Berris 2007.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef __NETWORK_MESSAGE_HPP__
#define __NETWORK_MESSAGE_HPP__

#include <boost/network/message_fwd.hpp>
#include <boost/network/traits/string.hpp>
#include <boost/network/traits/ostringstream.hpp>
#include <boost/network/traits/headers_container.hpp>
#include <boost/network/detail/directive_base.hpp>
#include <boost/network/detail/wrapper_base.hpp>
#include <boost/network/message/directives.hpp>
#include <boost/network/message/wrappers.hpp>
#include <boost/network/message/transformers.hpp>

#include <boost/network/message/modifiers/add_header.hpp>
#include <boost/network/message/modifiers/remove_header.hpp>
#include <boost/network/message/modifiers/clear_headers.hpp>
#include <boost/network/message/modifiers/source.hpp>
#include <boost/network/message/modifiers/destination.hpp>
#include <boost/network/message/modifiers/body.hpp>

#include <boost/network/message/message_concept.hpp>

/** message.hpp
 *
 * This header file implements the common message type which
 * all networking implementations under the boost::network
 * namespace. The common message type allows for easy message
 * construction and manipulation suited for networked
 * application development.
 */
namespace boost {
namespace network {

/** The common message type.
 */
template <class Tag>
struct basic_message {
 public:
  typedef Tag tag;

  typedef typename headers_container<Tag>::type headers_container_type;
  typedef typename headers_container_type::value_type header_type;
  typedef typename string<Tag>::type string_type;

  basic_message() : _headers(), _body(), _source(), _destination() {}

  basic_message(const basic_message& other)
      : _headers(other._headers),
        _body(other._body),
        _source(other._source),
        _destination(other._destination) {}

  basic_message& operator=(basic_message<Tag> rhs) {
    rhs.swap(*this);
    return *this;
  }

  void swap(basic_message<Tag>& other) {
    std::swap(other._headers, _headers);
    std::swap(other._body, _body);
    std::swap(other._source, _source);
    std::swap(other._destination, _destination);
  }

  headers_container_type& headers() { return _headers; }

  void headers(headers_container_type const& headers_) const {
    _headers = headers_;
  }

  void add_header(typename headers_container_type::value_type const& pair_)
      const {
    _headers.insert(pair_);
  }

  void remove_header(typename headers_container_type::key_type const& key)
      const {
    _headers.erase(key);
  }

  headers_container_type const& headers() const { return _headers; }

  string_type& body() { return _body; }

  void body(string_type const& body_) const { _body = body_; }

  string_type const& body() const { return _body; }

  string_type& source() { return _source; }

  void source(string_type const& source_) const { _source = source_; }

  string_type const& source() const { return _source; }

  string_type& destination() { return _destination; }

  void destination(string_type const& destination_) const {
    _destination = destination_;
  }

  string_type const& destination() const { return _destination; }

 private:
  friend struct detail::directive_base<Tag>;
  friend struct detail::wrapper_base<Tag, basic_message<Tag> >;

  mutable headers_container_type _headers;
  mutable string_type _body;
  mutable string_type _source;
  mutable string_type _destination;
};

template <class Tag>
inline void swap(basic_message<Tag>& left, basic_message<Tag>& right) {
  // swap for ADL
  left.swap(right);
}

// Commenting this out as we don't need to do this anymore.
// BOOST_CONCEPT_ASSERT((Message<basic_message<boost::network::tags::default_string>
// >));
// BOOST_CONCEPT_ASSERT((Message<basic_message<boost::network::tags::default_wstring>
// >));
typedef basic_message<tags::default_string> message;
typedef basic_message<tags::default_wstring> wmessage;

}  // namespace network
}  // namespace boost

#endif  // __NETWORK_MESSAGE_HPP__
