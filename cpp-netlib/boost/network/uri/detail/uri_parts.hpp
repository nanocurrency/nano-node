// Copyright 2009, 2010, 2011, 2012 Dean Michael Berris, Jeroen Habraken, Glyn
// Matthews.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_NETWORK_URL_DETAIL_URL_PARTS_HPP_
#define BOOST_NETWORK_URL_DETAIL_URL_PARTS_HPP_

#include <boost/range/iterator_range.hpp>
#include <boost/optional.hpp>

namespace boost {
namespace network {
namespace uri {
namespace detail {
template <class FwdIter>
struct hierarchical_part {
  optional<iterator_range<FwdIter> > user_info;
  optional<iterator_range<FwdIter> > host;
  optional<iterator_range<FwdIter> > port;
  optional<iterator_range<FwdIter> > path;

  FwdIter begin() const { return boost::begin(user_info); }

  FwdIter end() const { return boost::end(path); }

  void update() {
    if (!user_info) {
      if (host) {
        user_info = iterator_range<FwdIter>(boost::begin(host.get()),
                                            boost::begin(host.get()));
      } else if (path) {
        user_info = iterator_range<FwdIter>(boost::begin(path.get()),
                                            boost::begin(path.get()));
      }
    }

    if (!host) {
      host = iterator_range<FwdIter>(boost::begin(path.get()),
                                     boost::begin(path.get()));
    }

    if (!port) {
      port = iterator_range<FwdIter>(boost::end(host.get()),
                                     boost::end(host.get()));
    }

    if (!path) {
      path = iterator_range<FwdIter>(boost::end(port.get()),
                                     boost::end(port.get()));
    }
  }
};

template <class FwdIter>
struct uri_parts {
  iterator_range<FwdIter> scheme;
  hierarchical_part<FwdIter> hier_part;
  optional<iterator_range<FwdIter> > query;
  optional<iterator_range<FwdIter> > fragment;

  FwdIter begin() const { return boost::begin(scheme); }

  FwdIter end() const { return boost::end(fragment); }

  void update() {

    hier_part.update();

    if (!query) {
      query = iterator_range<FwdIter>(boost::end(hier_part.path.get()),
                                      boost::end(hier_part.path.get()));
    }

    if (!fragment) {
      fragment = iterator_range<FwdIter>(boost::end(query.get()),
                                         boost::end(query.get()));
    }
  }
};
}  // namespace detail
}  // namespace uri
}  // namespace network
}  // namespace boost

#endif  // BOOST_NETWORK_URL_DETAIL_URL_PARTS_HPP_
