#ifndef TAG_TYPES_4NNM8B5T
#define TAG_TYPES_4NNM8B5T

// Copyright 2010 Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/mpl/vector.hpp>
#include <boost/network/protocol/http/tags.hpp>

namespace http = boost::network::http;

typedef boost::mpl::vector<http::tags::http_default_8bit_tcp_resolve,
                           http::tags::http_default_8bit_udp_resolve,
                           http::tags::http_keepalive_8bit_tcp_resolve,
                           http::tags::http_keepalive_8bit_udp_resolve,
                           http::tags::http_async_8bit_udp_resolve,
                           http::tags::http_async_8bit_tcp_resolve> tag_types;

#endif /* TAG_TYPES_4NNM8B5T */
