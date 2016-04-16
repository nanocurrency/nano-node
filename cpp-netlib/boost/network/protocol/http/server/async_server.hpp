#ifndef BOOST_NETWORK_PROTOCOL_HTTP_SERVER_ASYNC_SERVER_HPP_20101025
#define BOOST_NETWORK_PROTOCOL_HTTP_SERVER_ASYNC_SERVER_HPP_20101025

// Copyright 2010 Dean Michael Berris.
// Copyright 2014 Jelle Van den Driessche.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/network/detail/debug.hpp>
#include <boost/network/protocol/http/server/async_connection.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/network/protocol/http/server/storage_base.hpp>
#include <boost/network/protocol/http/server/socket_options_base.hpp>
#include <boost/network/utils/thread_pool.hpp>

namespace boost {
namespace network {
namespace http {

template <class Tag, class Handler>
struct async_server_base : server_storage_base, socket_options_base {
  typedef basic_request<Tag> request;
  typedef basic_response<Tag> response;
  typedef typename string<Tag>::type string_type;
  typedef typename boost::network::http::response_header<Tag>::type
      response_header;
  typedef async_connection<Tag, Handler> connection;
  typedef shared_ptr<connection> connection_ptr;
  typedef boost::unique_lock<boost::mutex> scoped_mutex_lock;

  explicit async_server_base(server_options<Tag, Handler> const &options)
      : server_storage_base(options),
        socket_options_base(options),
        handler(options.handler()),
        address_(options.address()),
        port_(options.port()),
        thread_pool(options.thread_pool()
                        ? options.thread_pool()
                        : boost::make_shared<utils::thread_pool>()),
        acceptor(server_storage_base::service_),
        stopping(false),
        new_connection(),
        listening_mutex_(),
        stopping_mutex_(),
        listening(false),
        ctx_(options.context()) {}

  void run() {
    listen();
    service_.run();
  };

  void stop() {
    // stop accepting new requests and let all the existing
    // handlers finish.
    scoped_mutex_lock listening_lock(listening_mutex_);
    if (listening) {  // we dont bother stopping if we arent currently
                      // listening
      scoped_mutex_lock stopping_lock(stopping_mutex_);
      stopping = true;
      system::error_code ignored;
      acceptor.close(ignored);
      listening = false;
      service_.post(boost::bind(&async_server_base::handle_stop, this));
    }
  }

  void listen() {
    scoped_mutex_lock listening_lock(listening_mutex_);
    BOOST_NETWORK_MESSAGE("Listening on " << address_ << ':' << port_);
    if (!listening)
      start_listening();  // we only initialize our acceptor/sockets if we
                          // arent already listening
    if (!listening) {
      BOOST_NETWORK_MESSAGE("Error listening on " << address_ << ':' << port_);
      boost::throw_exception(
          std::runtime_error("Error listening on provided port."));
    }
  }

 private:
  Handler &handler;
  string_type address_, port_;
  boost::shared_ptr<utils::thread_pool> thread_pool;
  asio::ip::tcp::acceptor acceptor;
  bool stopping;
  connection_ptr new_connection;
  boost::mutex listening_mutex_;
  boost::mutex stopping_mutex_;
  bool listening;
  boost::shared_ptr<ssl_context> ctx_;

  void handle_stop() {
    scoped_mutex_lock stopping_lock(stopping_mutex_);
    if (stopping)
      service_.stop();  // a user may have started listening again before
                        // the stop command is reached
  }

  void handle_accept(boost::system::error_code const &ec) {
    {
      scoped_mutex_lock stopping_lock(stopping_mutex_);
      if (stopping)
        return;  // we dont want to add another handler instance, and we
                 // dont want to know about errors for a socket we dont
                 // need anymore
    }

    if (ec) {
      BOOST_NETWORK_MESSAGE("Error accepting connection, reason: " << ec);
    }

#ifdef BOOST_NETWORK_ENABLE_HTTPS
    socket_options_base::socket_options(new_connection->socket().next_layer());
#else
    socket_options_base::socket_options(new_connection->socket());
#endif

    new_connection->start();
    new_connection.reset(new connection(service_, handler, *thread_pool, ctx_));
    acceptor.async_accept(
#ifdef BOOST_NETWORK_ENABLE_HTTPS
        new_connection->socket().next_layer(),
#else
        new_connection->socket(),
#endif
        boost::bind(&async_server_base<Tag, Handler>::handle_accept, this,
                    boost::asio::placeholders::error));
  }

  void start_listening() {
    using boost::asio::ip::tcp;
    system::error_code error;
    service_.reset();  // this allows repeated cycles of run -> stop ->
                       // run
    tcp::resolver resolver(service_);
    tcp::resolver::query query(address_, port_);
    tcp::resolver::iterator endpoint_iterator = resolver.resolve(query, error);
    if (error) {
      BOOST_NETWORK_MESSAGE("Error resolving '" << address_ << ':' << port_);
      return;
    }
    tcp::endpoint endpoint = *endpoint_iterator;
    acceptor.open(endpoint.protocol(), error);
    if (error) {
      BOOST_NETWORK_MESSAGE("Error opening socket: " << address_ << ":"
                                                     << port_);
      return;
    }
    socket_options_base::acceptor_options(acceptor);
    acceptor.bind(endpoint, error);
    if (error) {
      BOOST_NETWORK_MESSAGE("Error binding socket: " << address_ << ":"
                                                     << port_);
      return;
    }
    acceptor.listen(asio::socket_base::max_connections, error);
    if (error) {
      BOOST_NETWORK_MESSAGE("Error listening on socket: '"
                            << error << "' on " << address_ << ":" << port_);
      return;
    }
    new_connection.reset(new connection(service_, handler, *thread_pool, ctx_));
    acceptor.async_accept(
#ifdef BOOST_NETWORK_ENABLE_HTTPS
        new_connection->socket().next_layer(),
#else
        new_connection->socket(),
#endif
        boost::bind(&async_server_base<Tag, Handler>::handle_accept, this,
                    boost::asio::placeholders::error));
    listening = true;
    scoped_mutex_lock stopping_lock(stopping_mutex_);
    stopping = false;  // if we were in the process of stopping, we revoke
                       // that command and continue listening
    BOOST_NETWORK_MESSAGE("Now listening on socket: '" << address_ << ":"
                                                       << port_ << "'");
  }
};

} /* http */

} /* network */

} /* boost */

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_SERVER_ASYNC_SERVER_HPP_20101025 */
