#ifndef NETLIB_IO_STREAM_HANDLER_HPP
#define NETLIB_IO_STREAM_HANDLER_HPP

// Copyright 2014 Jelle Van den Driessche.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#pragma once
#endif  // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <cstddef>
#include <boost/asio/detail/throw_error.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio.hpp>
#ifdef BOOST_NETWORK_ENABLE_HTTPS
#include <boost/asio/ssl.hpp>
#endif
#include <boost/asio/detail/push_options.hpp>
#include <boost/asio/detail/config.hpp>
#include <boost/asio/detail/handler_type_requirements.hpp>
#include <boost/asio/stream_socket_service.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/make_shared.hpp>
#include <boost/asio/detail/config.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/basic_socket.hpp>

namespace boost {
namespace network {

typedef boost::asio::ip::tcp::socket tcp_socket;

#ifndef BOOST_NETWORK_ENABLE_HTTPS
typedef tcp_socket stream_handler;
typedef void ssl_context;
#else

typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_socket;
typedef boost::asio::ssl::context ssl_context;

struct stream_handler {
 public:
  stream_handler(boost::shared_ptr<tcp_socket> socket)
      : tcp_sock_(socket), ssl_enabled(false) {}

  ~stream_handler() {}

  stream_handler(boost::shared_ptr<ssl_socket> socket)
      : ssl_sock_(socket), ssl_enabled(true) {}

  stream_handler(boost::asio::io_service& io,
                 boost::shared_ptr<ssl_context> ctx =
                     boost::shared_ptr<ssl_context>()) {
    tcp_sock_ = boost::make_shared<tcp_socket>(boost::ref(io));
    ssl_enabled = false;
    if (ctx) {
      /// SSL is enabled
      ssl_sock_ =
          boost::make_shared<ssl_socket>(boost::ref(io), boost::ref(*ctx));
      ssl_enabled = true;
    }
  }

  template <typename ConstBufferSequence, typename WriteHandler>
  BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler,
                                void(boost::system::error_code, std::size_t))
      async_write_some(const ConstBufferSequence& buffers,
                       BOOST_ASIO_MOVE_ARG(WriteHandler) handler) {
    try {
      if (ssl_enabled) {
        ssl_sock_->async_write_some(buffers, handler);
      } else {
        tcp_sock_->async_write_some(buffers, handler);
      }
    }
    catch (const boost::system::error_code& e) {
      std::cerr << e.message() << std::endl;
    }
    catch (const boost::system::system_error& e) {
      std::cerr << e.code() << ": " << e.what() << std::endl;
    }
  }

  template <typename MutableBufferSequence, typename ReadHandler>
  BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler,
                                void(boost::system::error_code, std::size_t))
      async_read_some(const MutableBufferSequence& buffers,
                      BOOST_ASIO_MOVE_ARG(ReadHandler) handler) {
    try {
      if (ssl_enabled) {
        ssl_sock_->async_read_some(buffers, handler);
      } else {
        tcp_sock_->async_read_some(buffers, handler);
      }
    }
    catch (const boost::system::error_code& e) {
      std::cerr << e.message() << std::endl;
    }
    catch (const boost::system::system_error& e) {
      std::cerr << e.code() << ": " << e.what() << std::endl;
    }
  }

  void close(boost::system::error_code& e) {
    if (ssl_enabled) {
      ssl_sock_->next_layer().close();
    } else {
      tcp_sock_->close();
    }
  }

  tcp_socket::endpoint_type remote_endpoint() const {
    if (ssl_enabled) {
      return ssl_sock_->next_layer().remote_endpoint();
    } else {
      return tcp_sock_->remote_endpoint();
    }
  }

  void shutdown(boost::asio::socket_base::shutdown_type st,
                boost::system::error_code& e) {
    try {
      if (ssl_enabled) {
        ssl_sock_->shutdown(e);
      } else {
        tcp_sock_->shutdown(boost::asio::ip::tcp::socket::shutdown_send, e);
      }
    }
    catch (const boost::system::error_code& e) {
      std::cerr << e.message() << std::endl;
    }
    catch (const boost::system::system_error& e) {
      std::cerr << e.code() << ": " << e.what() << std::endl;
    }
  }

  ssl_socket::next_layer_type& next_layer() const {
    if (ssl_enabled) {
      return ssl_sock_->next_layer();
    } else {
      return *tcp_sock_;
    }
  }

  ssl_socket::lowest_layer_type& lowest_layer() const {
    if (ssl_enabled) {
      return ssl_sock_->lowest_layer();
    } else {
      return tcp_sock_->lowest_layer();
    }
  }

  template <typename HandshakeHandler>
  BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler,
                                void(boost::system::error_code))
      async_handshake(ssl_socket::handshake_type type,
                      BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler) {
    try {
      if (ssl_enabled) {
        return ssl_sock_->async_handshake(type, handler);
      } else {
        // NOOP
      }
    }
    catch (const boost::system::error_code& e) {
      std::cerr << e.message() << std::endl;
    }
    catch (const boost::system::system_error& e) {
      std::cerr << e.code() << ": " << e.what() << std::endl;
    }
  }
  boost::shared_ptr<tcp_socket> get_tcp_socket() { return tcp_sock_; }
  boost::shared_ptr<ssl_socket> get_ssl_socket() { return ssl_sock_; }

  bool is_ssl_enabled() { return ssl_enabled; }

 private:
  boost::shared_ptr<tcp_socket> tcp_sock_;
  boost::shared_ptr<ssl_socket> ssl_sock_;
  bool ssl_enabled;
};
#endif
}
}

#endif
