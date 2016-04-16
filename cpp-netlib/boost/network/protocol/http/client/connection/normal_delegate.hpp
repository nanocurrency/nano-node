#ifndef BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_CONNECTION_NORMAL_DELEGATE_20110819
#define BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_CONNECTION_NORMAL_DELEGATE_20110819

// Copyright 2011 Dean Michael Berris (dberris@google.com).
// Copyright 2011 Google, Inc.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/network/protocol/http/client/connection/connection_delegate.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/asio/placeholders.hpp>

namespace boost {
namespace network {
namespace http {
namespace impl {

struct normal_delegate : connection_delegate {
  normal_delegate(asio::io_service &service);

  virtual void connect(asio::ip::tcp::endpoint &endpoint, std::string host, boost::uint16_t source_port,
                       function<void(system::error_code const &)> handler);
  virtual void write(
      asio::streambuf &command_streambuf,
      function<void(system::error_code const &, size_t)> handler);
  virtual void read_some(
      asio::mutable_buffers_1 const &read_buffer,
      function<void(system::error_code const &, size_t)> handler);
  virtual void disconnect();
  ~normal_delegate();

 private:
  asio::io_service &service_;
  scoped_ptr<asio::ip::tcp::socket> socket_;

  normal_delegate(normal_delegate const &);     // = delete
  normal_delegate &operator=(normal_delegate);  // = delete
};

} /* impl */

} /* http */

} /* network */

} /* boost */

#ifdef BOOST_NETWORK_NO_LIB
#include <boost/network/protocol/http/client/connection/normal_delegate.ipp>
#endif /* BOOST_NETWORK_NO_LIB */

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_CONNECTION_NORMAL_DELEGATE_20110819 \
          */
