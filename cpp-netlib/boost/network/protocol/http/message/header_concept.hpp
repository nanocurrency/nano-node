#ifndef BOOST_NETWORK_PROTOCOL_HTTP_MESSAGE_HEADER_CONCEPT_HPP_20101028
#define BOOST_NETWORK_PROTOCOL_HTTP_MESSAGE_HEADER_CONCEPT_HPP_20101028

// Copyright 2010 Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/network/traits/string.hpp>

namespace boost {
namespace network {
namespace http {

template <class H>
struct Header : DefaultConstructible<H>, Assignable<H>, CopyConstructible<H> {

  BOOST_CONCEPT_USAGE(Header) {
    typedef typename H::string_type string_type;
    string_type name_ = name(header);
    string_type value_ = value(header);
    H h1, h2;
    swap(h1, h2);  // ADL Swap!
    (void)name_;
    (void)value_;
  }

 private:
  H header;
};

} /* http */

} /* network */

} /* boost */

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_MESSAGE_HEADER_CONCEPT_HPP_20101028 */
