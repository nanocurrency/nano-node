#ifndef BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_MACROS_HPP_20110430
#define BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_MACROS_HPP_20110430

// Copyright 2011 Dean Michael Berris <mikhailberis@gmail.com>.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/range/iterator_range.hpp>
#include <boost/system/error_code.hpp>

#ifndef BOOST_NETWORK_HTTP_BODY_CALLBACK
#define BOOST_NETWORK_HTTP_BODY_CALLBACK(function_name, range_name,        \
                                         error_name)                       \
  void function_name(boost::iterator_range<const char*> const& range_name, \
                     boost::system::error_code const& error_name)
#endif

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_MACROS_HPP_20110430 */
