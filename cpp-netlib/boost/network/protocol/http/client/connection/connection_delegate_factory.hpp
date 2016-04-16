#ifndef BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_CONNECTION_DELEGATE_FACTORY_HPP_20110819
#define BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_CONNECTION_DELEGATE_FACTORY_HPP_20110819

// Copyright 2011 Dean Michael Berris (dberris@google.com).
// Copyright 2011 Google, Inc.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/throw_exception.hpp>
#include <boost/network/protocol/http/client/connection/connection_delegate.hpp>
#include <boost/network/protocol/http/client/connection/normal_delegate.hpp>
#ifdef BOOST_NETWORK_ENABLE_HTTPS
#include <boost/network/protocol/http/client/connection/ssl_delegate.hpp>
#endif /* BOOST_NETWORK_ENABLE_HTTPS */

namespace boost {
namespace network {
namespace http {
namespace impl {

struct ssl_delegate;

struct normal_delegate;

template <class Tag>
struct connection_delegate_factory {
  typedef shared_ptr<connection_delegate> connection_delegate_ptr;
  typedef typename string<Tag>::type string_type;

  // This is the factory method that actually returns the delegate
  // instance.
  // TODO Support passing in proxy settings when crafting connections.
  static connection_delegate_ptr new_connection_delegate(
      asio::io_service& service, bool https, bool always_verify_peer,
      optional<string_type> certificate_filename,
      optional<string_type> verify_path, optional<string_type> certificate_file,
      optional<string_type> private_key_file, optional<string_type> ciphers,
      long ssl_options) {
    connection_delegate_ptr delegate;
    if (https) {
#ifdef BOOST_NETWORK_ENABLE_HTTPS
      delegate.reset(new ssl_delegate(
          service, always_verify_peer, certificate_filename, verify_path,
          certificate_file, private_key_file, ciphers, ssl_options));
#else
      BOOST_THROW_EXCEPTION(std::runtime_error("HTTPS not supported."));
#endif /* BOOST_NETWORK_ENABLE_HTTPS */
    } else {
      delegate.reset(new normal_delegate(service));
    }
    return delegate;
  }
};

} /* impl */
} /* http */
} /* network */
} /* boost */

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_CONNECTION_DELEGATE_FACTORY_HPP_20110819 \
          */
