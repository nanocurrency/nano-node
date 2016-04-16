
// Copyright 2010 Dean Michael Berris.
// Copyright 2010 Glyn Matthews.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_NETWORK_PROTOCOL_HTTP_SERVER_SYNC_SERVER_HPP_20101025
#define BOOST_NETWORK_PROTOCOL_HTTP_SERVER_SYNC_SERVER_HPP_20101025

#include <boost/network/detail/debug.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/network/protocol/http/response.hpp>
#include <boost/network/protocol/http/request.hpp>
#include <boost/network/protocol/http/server/sync_connection.hpp>
#include <boost/network/protocol/http/server/storage_base.hpp>
#include <boost/network/protocol/http/server/socket_options_base.hpp>
#include <boost/network/protocol/http/server/options.hpp>
#include <boost/network/traits/string.hpp>
#include <boost/thread/mutex.hpp>

namespace boost {
namespace network {
namespace http {

template <class Tag, class Handler>
struct sync_server_base : server_storage_base, socket_options_base {
  typedef typename string<Tag>::type string_type;
  typedef basic_request<Tag> request;
  typedef basic_response<Tag> response;
  typedef typename boost::network::http::response_header<Tag>::type
      response_header;

  sync_server_base(server_options<Tag, Handler> const& options)
      : server_storage_base(options),
        socket_options_base(options),
        handler_(options.handler()),
        address_(options.address()),
        port_(options.port()),
        acceptor_(server_storage_base::service_),
        new_connection(),
        listening_mutex_(),
        listening_(false) {}

  void run() {
    listen();
    service_.run();
  }

  void stop() {
    // stop accepting new connections and let all the existing handlers
    // finish.
    system::error_code ignored;
    acceptor_.close(ignored);
    service_.stop();
  }

  void listen() {
    boost::unique_lock<boost::mutex> listening_lock(listening_mutex_);
    if (!listening_) start_listening();
  }

 private:
  Handler& handler_;
  string_type address_, port_;
  boost::asio::ip::tcp::acceptor acceptor_;
  boost::shared_ptr<sync_connection<Tag, Handler> > new_connection;
  boost::mutex listening_mutex_;
  bool listening_;

  void handle_accept(boost::system::error_code const& ec) {
    if (ec) {
    }
    socket_options_base::socket_options(new_connection->socket());
    new_connection->start();
    new_connection.reset(new sync_connection<Tag, Handler>(service_, handler_));
    acceptor_.async_accept(
        new_connection->socket(),
        boost::bind(&sync_server_base<Tag, Handler>::handle_accept, this,
                    boost::asio::placeholders::error));
  }

  void start_listening() {
    using boost::asio::ip::tcp;
    system::error_code error;
    tcp::resolver resolver(service_);
    tcp::resolver::query query(address_, port_);
    tcp::resolver::iterator endpoint_iterator = resolver.resolve(query, error);
    if (error) {
      BOOST_NETWORK_MESSAGE("Error resolving address: " << address_ << ':'
                                                        << port_);
      boost::throw_exception(std::runtime_error("Error resolving address."));
    }
    tcp::endpoint endpoint = *endpoint_iterator;
    acceptor_.open(endpoint.protocol(), error);
    if (error) {
      BOOST_NETWORK_MESSAGE("Error opening socket: " << address_ << ':' << port_
                                                     << " -- reason: '" << error
                                                     << '\'');
      boost::throw_exception(std::runtime_error("Error opening socket."));
    }
    socket_options_base::acceptor_options(acceptor_);
    acceptor_.bind(endpoint, error);
    if (error) {
      BOOST_NETWORK_MESSAGE("Error binding to socket: "
                            << address_ << ':' << port_ << " -- reason: '"
                            << error << '\'');
      boost::throw_exception(std::runtime_error("Error binding to socket."));
    }
    acceptor_.listen(tcp::socket::max_connections, error);
    if (error) {
      BOOST_NETWORK_MESSAGE("Error listening on socket: "
                            << address_ << ':' << port_ << " -- reason: '"
                            << error << '\'');
      boost::throw_exception(std::runtime_error("Error listening on socket."));
    }
    new_connection.reset(new sync_connection<Tag, Handler>(service_, handler_));
    acceptor_.async_accept(
        new_connection->socket(),
        boost::bind(&sync_server_base<Tag, Handler>::handle_accept, this,
                    boost::asio::placeholders::error));
    listening_ = true;
  }
};

} /* http */

} /* network */

} /* boost */

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_SERVER_SYNC_SERVER_HPP_20101025 */
