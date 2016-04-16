#ifndef BOOST_NETWORK_PROTOCOL_HTTP_SERVER_OPTIONS_20130128
#define BOOST_NETWORK_PROTOCOL_HTTP_SERVER_OPTIONS_20130128

// Copyright 2013 Google, Inc.
// Copyright 2013 Dean Michael Berris <dberris@google.com>
// Copyright 2014 Jelle Van den Driessche
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/asio/io_service.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/network/protocol/stream_handler.hpp>
#include <boost/network/traits/string.hpp>
#include <boost/network/utils/thread_pool.hpp>
#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>

namespace boost {
namespace network {
namespace http {

template <class Tag, class Handler>
struct server_options {
  typedef typename string<Tag>::type string_type;

  explicit server_options(Handler &handler)
      : io_service_(),
        handler_(handler),
        address_("localhost"),
        port_("80"),
        reuse_address_(false),
        report_aborted_(false),
        non_blocking_io_(true),
        linger_(true),
        linger_timeout_(0),
        receive_buffer_size_(),
        send_buffer_size_(),
        receive_low_watermark_(),
        send_low_watermark_(),
        thread_pool_(),
        context_() {}

  server_options(const server_options &other)
      : io_service_(other.io_service()),
        handler_(other.handler_),
        address_(other.address_),
        port_(other.port_),
        reuse_address_(other.reuse_address_),
        report_aborted_(other.report_aborted_),
        non_blocking_io_(other.non_blocking_io_),
        linger_(other.linger_),
        linger_timeout_(0),
        receive_buffer_size_(other.receive_buffer_size_),
        send_buffer_size_(other.send_buffer_size_),
        receive_low_watermark_(other.receive_low_watermark_),
        send_low_watermark_(other.send_low_watermark_),
        thread_pool_(other.thread_pool_),
        context_(other.context_) {}

  server_options &operator=(server_options other) {
    other.swap(*this);
    return *this;
  }

  void swap(server_options &other) {
    using std::swap;
    swap(io_service_, other.io_service_);
    swap(address_, other.address_);
    swap(port_, other.port_);
    swap(reuse_address_, other.reuse_address_);
    swap(report_aborted_, other.report_aborted_);
    swap(non_blocking_io_, other.non_blocking_io_);
    swap(linger_, other.linger_);
    swap(linger_timeout_, other.linger_timeout_);
    swap(receive_buffer_size_, other.receive_buffer_size_);
    swap(send_buffer_size_, other.send_buffer_size_);
    swap(receive_low_watermark_, other.receive_low_watermark_);
    swap(send_low_watermark_, other.send_low_watermark_);
    swap(thread_pool_, other.thread_pool_);
    swap(context_, other.context_);
  }

  server_options &context(boost::shared_ptr<ssl_context> v) {
    context_ = v;
    return *this;
  }
  server_options &io_service(boost::shared_ptr<boost::asio::io_service> v) {
    io_service_ = v;
    return *this;
  }
  server_options &address(string_type const &v) {
    address_ = v;
    return *this;
  }
  server_options &port(string_type const &v) {
    port_ = v;
    return *this;
  }
  server_options &reuse_address(bool v) {
    reuse_address_ = v;
    return *this;
  }
  server_options &report_aborted(bool v) {
    report_aborted_ = v;
    return *this;
  }
  server_options &non_blocking_io(bool v) {
    non_blocking_io_ = v;
    return *this;
  }
  server_options &linger(bool v) {
    linger_ = v;
    return *this;
  }
  server_options &linger_timeout(size_t v) {
    linger_timeout_ = v;
    return *this;
  }
  server_options &receive_buffer_size(
      boost::asio::socket_base::receive_buffer_size v) {
    receive_buffer_size_ = v;
    return *this;
  }
  server_options &send_buffer_size(
      boost::asio::socket_base::send_buffer_size v) {
    send_buffer_size_ = v;
    return *this;
  }
  server_options &receive_low_watermark(
      boost::asio::socket_base::receive_low_watermark v) {
    receive_low_watermark_ = v;
    return *this;
  }
  server_options &send_low_watermark(
      boost::asio::socket_base::send_low_watermark v) {
    send_low_watermark_ = v;
    return *this;
  }
  server_options &thread_pool(boost::shared_ptr<utils::thread_pool> v) {
    thread_pool_ = v;
    return *this;
  }

  boost::shared_ptr<boost::asio::io_service> io_service() const {
    return io_service_;
  }
  string_type address() const { return address_; }
  string_type port() const { return port_; }
  Handler &handler() const { return handler_; }
  bool reuse_address() const { return reuse_address_; }
  bool report_aborted() const { return report_aborted_; }
  bool non_blocking_io() const { return non_blocking_io_; }
  bool linger() const { return linger_; }
  size_t linger_timeout() const { return linger_timeout_; }
  boost::optional<boost::asio::socket_base::receive_buffer_size>
  receive_buffer_size() const {
    return receive_buffer_size_;
  }
  boost::optional<boost::asio::socket_base::send_buffer_size> send_buffer_size()
      const {
    return send_buffer_size_;
  }
  boost::optional<boost::asio::socket_base::receive_low_watermark>
  receive_low_watermark() const {
    return receive_low_watermark_;
  }
  boost::optional<boost::asio::socket_base::send_low_watermark>
  send_low_watermark() const {
    return send_low_watermark_;
  }
  boost::shared_ptr<utils::thread_pool> thread_pool() const {
    return thread_pool_;
  }
  boost::shared_ptr<ssl_context> context() const {
    return context_;
  }

 private:
  boost::shared_ptr<boost::asio::io_service> io_service_;
  Handler &handler_;
  string_type address_;
  string_type port_;
  bool reuse_address_;
  bool report_aborted_;
  bool non_blocking_io_;
  bool linger_;
  size_t linger_timeout_;
  boost::optional<boost::asio::socket_base::receive_buffer_size>
      receive_buffer_size_;
  boost::optional<boost::asio::socket_base::send_buffer_size> send_buffer_size_;
  boost::optional<boost::asio::socket_base::receive_low_watermark>
      receive_low_watermark_;
  boost::optional<boost::asio::socket_base::send_low_watermark>
      send_low_watermark_;
  boost::shared_ptr<utils::thread_pool> thread_pool_;
  boost::shared_ptr<ssl_context> context_;
};

template <class Tag, class Handler>
inline void swap(server_options<Tag, Handler> &a,
                 server_options<Tag, Handler> &b) {
  a.swap(b);
}

} /* http */
} /* network */
} /* boost */

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_SERVER_OPTIONS_20130128 */
