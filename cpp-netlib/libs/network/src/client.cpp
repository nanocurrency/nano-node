
// Copyright 2011 Dean Michael Berris (dberris@google.com).
// Copyright 2011 Google, Inc.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#ifdef BOOST_NETWORK_NO_LIB
#warn Building the library even with BOOST_NETWORK_NO_LIB defined.
#undef BOOST_NETWORK_NO_LIB
#endif

#include <boost/network/protocol/http/client/connection/normal_delegate.ipp>

#ifdef BOOST_NETWORK_ENABLE_HTTPS
#include <boost/network/protocol/http/client/connection/ssl_delegate.ipp>
#endif
