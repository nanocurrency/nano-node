#ifndef BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_CONNECTION_SSL_DELEGATE_20110819
#define BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_CONNECTION_SSL_DELEGATE_20110819

// Copyright 2011 Dean Michael Berris (dberris@google.com).
// Copyright 2011 Google, Inc.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/asio/io_service.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/network/protocol/http/client/connection/connection_delegate.hpp>
#include <boost/optional.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/network/support/is_default_string.hpp>
#include <boost/network/support/is_default_wstring.hpp>

namespace boost {
namespace network {
namespace http {
namespace impl {

struct ssl_delegate : connection_delegate,
                      enable_shared_from_this<ssl_delegate> {
  ssl_delegate(asio::io_service &service, bool always_verify_peer,
               optional<std::string> certificate_filename,
               optional<std::string> verify_path,
               optional<std::string> certificate_file,
               optional<std::string> private_key_file,
               optional<std::string> ciphers, long ssl_options);

  virtual void connect(asio::ip::tcp::endpoint &endpoint, std::string host, boost::uint16_t source_port,
                       function<void(system::error_code const &)> handler);
  virtual void write(
      asio::streambuf &command_streambuf,
      function<void(system::error_code const &, size_t)> handler);
  virtual void read_some(
      asio::mutable_buffers_1 const &read_buffer,
      function<void(system::error_code const &, size_t)> handler);
  virtual void disconnect();
  ~ssl_delegate();

 private:
  asio::io_service &service_;
  optional<std::string> certificate_filename_;
  optional<std::string> verify_path_;
  optional<std::string> certificate_file_;
  optional<std::string> private_key_file_;
  optional<std::string> ciphers_;
  long ssl_options_;
  scoped_ptr<asio::ssl::context> context_;
  scoped_ptr<asio::ip::tcp::socket> tcp_socket_;
  scoped_ptr<asio::ssl::stream<asio::ip::tcp::socket&> > socket_;
  bool always_verify_peer_;

  ssl_delegate(ssl_delegate const &);     // = delete
  ssl_delegate &operator=(ssl_delegate);  // = delete

  void handle_connected(system::error_code const &ec,
                        function<void(system::error_code const &)> handler);
};

} /* impl */

} /* http */

} /* network */

} /* boost */

#ifdef BOOST_NETWORK_NO_LIB
#include <boost/network/protocol/http/client/connection/ssl_delegate.ipp>
#endif /* BOOST_NETWORK_NO_LIB */

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_CONNECTION_SSL_DELEGATE_20110819 \
          */
