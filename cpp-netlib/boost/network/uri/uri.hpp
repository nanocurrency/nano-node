// Copyright 2009, 2010, 2011, 2012 Dean Michael Berris, Jeroen Habraken, Glyn
// Matthews.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef __BOOST_NETWORK_URI_INC__
#define __BOOST_NETWORK_URI_INC__

#pragma once

#include <boost/network/uri/config.hpp>
#include <boost/network/uri/detail/uri_parts.hpp>
#include <boost/network/uri/schemes.hpp>
#include <boost/utility/swap.hpp>
#include <boost/range/algorithm/equal.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/as_literal.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <boost/functional/hash_fwd.hpp>

namespace boost {
namespace network {
namespace uri {
namespace detail {
bool parse(std::string::const_iterator first, std::string::const_iterator last,
           uri_parts<std::string::const_iterator> &parts);
}  // namespace detail

class BOOST_URI_DECL uri {

  friend class builder;

 public:
  typedef std::string string_type;
  typedef string_type::value_type value_type;
  typedef string_type::const_iterator const_iterator;
  typedef boost::iterator_range<const_iterator> const_range_type;

  uri() : is_valid_(false) {}

  // uri(const value_type *uri)
  //    : uri_(uri), is_valid_(false) {
  //    parse();
  //}

  uri(const string_type &uri) : uri_(uri), is_valid_(false) { parse(); }

  template <class FwdIter>
  uri(const FwdIter &first, const FwdIter &last)
      : uri_(first, last), is_valid_(false) {
    parse();
  }

  uri(const uri &other) : uri_(other.uri_) { parse(); }

  uri &operator=(const uri &other) {
    uri(other).swap(*this);
    return *this;
  }

  uri &operator=(const string_type &uri_string) {
    uri(uri_string).swap(*this);
    return *this;
  }

  ~uri() {}

  void swap(uri &other) {
    boost::swap(uri_, other.uri_);
    other.parse();
    boost::swap(is_valid_, other.is_valid_);
  }

  const_iterator begin() const { return uri_.begin(); }

  const_iterator end() const { return uri_.end(); }

  const_range_type scheme_range() const { return uri_parts_.scheme; }

  const_range_type user_info_range() const {
    return uri_parts_.hier_part.user_info ? uri_parts_.hier_part.user_info.get()
                                          : const_range_type();
  }

  const_range_type host_range() const {
    auto result = uri_parts_.hier_part.host ? uri_parts_.hier_part.host.get()
                                     : const_range_type();
	if (result.begin () != result.end ())
		if (result [0] == '[')
			result = {result.begin () + 1, result.end () - 1};
	return result;
  }

  const_range_type port_range() const {
    return uri_parts_.hier_part.port ? uri_parts_.hier_part.port.get()
                                     : const_range_type();
  }

  const_range_type path_range() const {
    return uri_parts_.hier_part.path ? uri_parts_.hier_part.path.get()
                                     : const_range_type();
  }

  const_range_type query_range() const {
    return uri_parts_.query ? uri_parts_.query.get() : const_range_type();
  }

  const_range_type fragment_range() const {
    return uri_parts_.fragment ? uri_parts_.fragment.get() : const_range_type();
  }

  string_type scheme() const {
    const_range_type range = scheme_range();
    return range ? string_type(boost::begin(range), boost::end(range))
                 : string_type();
  }

  string_type user_info() const {
    const_range_type range = user_info_range();
    return range ? string_type(boost::begin(range), boost::end(range))
                 : string_type();
  }

  string_type host() const {
    const_range_type range = host_range();
    return range ? string_type(boost::begin(range), boost::end(range))
                 : string_type();
  }

  string_type port() const {
    const_range_type range = port_range();
    return range ? string_type(boost::begin(range), boost::end(range))
                 : string_type();
  }

  string_type path() const {
    const_range_type range = path_range();
    return range ? string_type(boost::begin(range), boost::end(range))
                 : string_type();
  }

  string_type query() const {
    const_range_type range = query_range();
    return range ? string_type(boost::begin(range), boost::end(range))
                 : string_type();
  }

  string_type fragment() const {
    const_range_type range = fragment_range();
    return range ? string_type(boost::begin(range), boost::end(range))
                 : string_type();
  }

  string_type string() const { return uri_; }

  bool is_valid() const { return is_valid_; }

  void append(const string_type &data) {
    uri_.append(data);
    parse();
  }

  template <class FwdIter>
  void append(const FwdIter &first, const FwdIter &last) {
    uri_.append(first, last);
    parse();
  }

 private:
  void parse();

  string_type uri_;
  detail::uri_parts<const_iterator> uri_parts_;
  bool is_valid_;
};

inline void uri::parse() {
  const_iterator first(boost::begin(uri_)), last(boost::end(uri_));
  is_valid_ = detail::parse(first, last, uri_parts_);
  if (is_valid_) {
    if (!uri_parts_.scheme) {
      uri_parts_.scheme =
          const_range_type(boost::begin(uri_), boost::begin(uri_));
    }
    uri_parts_.update();
  }
}

inline uri::string_type scheme(const uri &uri_) { return uri_.scheme(); }

inline uri::string_type user_info(const uri &uri_) { return uri_.user_info(); }

inline uri::string_type host(const uri &uri_) { return uri_.host(); }

inline uri::string_type port(const uri &uri_) { return uri_.port(); }

inline boost::optional<unsigned short> port_us(const uri &uri_) {
  uri::string_type port = uri_.port();
  return (port.empty()) ? boost::optional<unsigned short>()
                        : boost::optional<unsigned short>(
                              boost::lexical_cast<unsigned short>(port));
}

inline uri::string_type path(const uri &uri_) { return uri_.path(); }

inline uri::string_type query(const uri &uri_) { return uri_.query(); }

inline uri::string_type fragment(const uri &uri_) { return uri_.fragment(); }

inline uri::string_type hierarchical_part(const uri &uri_) {
  uri::string_type::const_iterator first, last;
  uri::const_range_type user_info = uri_.user_info_range();
  uri::const_range_type host = uri_.host_range();
  uri::const_range_type port = uri_.port_range();
  uri::const_range_type path = uri_.path_range();
  if (user_info) {
    first = boost::begin(user_info);
  } else {
    first = boost::begin(host);
  }
  if (path) {
    last = boost::end(path);
  } else if (port) {
    last = boost::end(port);
  } else {
    last = boost::end(host);
  }
  return uri::string_type(first, last);
}

inline uri::string_type authority(const uri &uri_) {
  uri::string_type::const_iterator first, last;
  uri::const_range_type user_info = uri_.user_info_range();
  uri::const_range_type host = uri_.host_range();
  uri::const_range_type port = uri_.port_range();
  if (user_info) {
    first = boost::begin(user_info);
  } else {
    first = boost::begin(host);
  }

  if (port) {
    last = boost::end(port);
  } else {
    last = boost::end(host);
  }
  return uri::string_type(first, last);
}

inline bool valid(const uri &uri_) { return uri_.is_valid(); }

inline bool is_absolute(const uri &uri_) {
  return uri_.is_valid() && !boost::empty(uri_.scheme_range());
}

inline bool is_relative(const uri &uri_) {
  return uri_.is_valid() && boost::empty(uri_.scheme_range());
}

inline bool is_hierarchical(const uri &uri_) {
  return is_absolute(uri_) && hierarchical_schemes::exists(scheme(uri_));
}

inline bool is_opaque(const uri &uri_) {
  return is_absolute(uri_) && opaque_schemes::exists(scheme(uri_));
}

inline bool is_valid(const uri &uri_) { return valid(uri_); }

inline void swap(uri &lhs, uri &rhs) { lhs.swap(rhs); }

inline std::size_t hash_value(const uri &uri_) {
  std::size_t seed = 0;
  for (uri::const_iterator it = begin(uri_); it != end(uri_); ++it) {
    hash_combine(seed, *it);
  }
  return seed;
}

inline bool operator==(const uri &lhs, const uri &rhs) {
  return boost::equal(lhs, rhs);
}

inline bool operator==(const uri &lhs, const uri::string_type &rhs) {
  return boost::equal(lhs, rhs);
}

inline bool operator==(const uri::string_type &lhs, const uri &rhs) {
  return boost::equal(lhs, rhs);
}

inline bool operator==(const uri &lhs, const uri::value_type *rhs) {
  return boost::equal(lhs, boost::as_literal(rhs));
}

inline bool operator==(const uri::value_type *lhs, const uri &rhs) {
  return boost::equal(boost::as_literal(lhs), rhs);
}

inline bool operator!=(const uri &lhs, const uri &rhs) { return !(lhs == rhs); }

inline bool operator<(const uri &lhs, const uri &rhs) {
  return lhs.string() < rhs.string();
}
}  // namespace uri
}  // namespace network
}  // namespace boost

#include <boost/network/uri/accessors.hpp>
#include <boost/network/uri/directives.hpp>
#include <boost/network/uri/builder.hpp>

namespace boost {
namespace network {
namespace uri {
inline uri from_parts(const uri &base_uri, const uri::string_type &path_,
                      const uri::string_type &query_,
                      const uri::string_type &fragment_) {
  uri uri_(base_uri);
  builder(uri_).path(path_).query(query_).fragment(fragment_);
  return uri_;
}

inline uri from_parts(const uri &base_uri, const uri::string_type &path_,
                      const uri::string_type &query_) {
  uri uri_(base_uri);
  builder(uri_).path(path_).query(query_);
  return uri_;
}

inline uri from_parts(const uri &base_uri, const uri::string_type &path_) {
  uri uri_(base_uri);
  builder(uri_).path(path_);
  return uri_;
}

inline uri from_parts(const uri::string_type &base_uri,
                      const uri::string_type &path,
                      const uri::string_type &query,
                      const uri::string_type &fragment) {
  return from_parts(uri(base_uri), path, query, fragment);
}

inline uri from_parts(const uri::string_type &base_uri,
                      const uri::string_type &path,
                      const uri::string_type &query) {
  return from_parts(uri(base_uri), path, query);
}

inline uri from_parts(const uri::string_type &base_uri,
                      const uri::string_type &path) {
  return from_parts(uri(base_uri), path);
}
}  // namespace uri
}  // namespace network
}  // namespace boost

#include <boost/filesystem/path.hpp>

namespace boost {
namespace network {
namespace uri {
inline uri from_file(const filesystem::path &path_) {
  uri uri_;
  builder(uri_).scheme("file").path(path_.string());
  return uri_;
}
}  // namespace uri
}  // namespace network
}  // namespace boost

#endif  // __BOOST_NETWORK_URI_INC__
