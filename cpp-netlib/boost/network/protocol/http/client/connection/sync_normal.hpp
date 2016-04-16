#ifndef BOOST_NETWORK_PROTOCOL_HTTP_IMPL_HTTP_SYNC_CONNECTION_20100601
#define BOOST_NETWORK_PROTOCOL_HTTP_IMPL_HTTP_SYNC_CONNECTION_20100601

// Copyright 2013 Google, Inc.
// Copyright 2010 (C) Dean Michael Berris
// Copyright 2010 (C) Sinefunc, Inc.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/network/protocol/http/algorithms/linearize.hpp>
#include <iterator>

#include <boost/asio/deadline_timer.hpp>

namespace boost {
namespace network {
namespace http {
namespace impl {

template <class Tag, unsigned version_major, unsigned version_minor>
struct sync_connection_base_impl;

template <class Tag, unsigned version_major, unsigned version_minor>
struct sync_connection_base;

template <class Tag, unsigned version_major, unsigned version_minor>
struct http_sync_connection
    : public virtual sync_connection_base<Tag, version_major, version_minor>,
      sync_connection_base_impl<Tag, version_major, version_minor>,
      boost::enable_shared_from_this<
          http_sync_connection<Tag, version_major, version_minor> > {
  typedef typename resolver_policy<Tag>::type resolver_base;
  typedef typename resolver_base::resolver_type resolver_type;
  typedef typename string<Tag>::type string_type;
  typedef function<typename resolver_base::resolver_iterator_pair(
      resolver_type&, string_type const&, string_type const&)>
      resolver_function_type;
  typedef http_sync_connection<Tag, version_major, version_minor> this_type;
  typedef sync_connection_base_impl<Tag, version_major, version_minor>
      connection_base;
  typedef function<bool(string_type&)> body_generator_function_type;

  http_sync_connection(resolver_type& resolver, resolver_function_type resolve,
                       int timeout)
      : connection_base(),
        timeout_(timeout),
        timer_(resolver.get_io_service()),
        resolver_(resolver),
        resolve_(resolve),
        socket_(resolver.get_io_service()) {}

  void init_socket(string_type const& hostname, string_type const& port) {
    connection_base::init_socket(socket_, resolver_, hostname, port, resolve_);
  }

  void send_request_impl(string_type const& method,
                         basic_request<Tag> const& request_,
                         body_generator_function_type generator) {
    boost::asio::streambuf request_buffer;
    linearize(
        request_, method, version_major, version_minor,
        std::ostreambuf_iterator<typename char_<Tag>::type>(&request_buffer));
    connection_base::send_request_impl(socket_, method, request_buffer);
    if (generator) {
      string_type chunk;
      while (generator(chunk)) {
        std::copy(chunk.begin(), chunk.end(),
                  std::ostreambuf_iterator<typename char_<Tag>::type>(
                      &request_buffer));
        chunk.clear();
        connection_base::send_request_impl(socket_, method, request_buffer);
      }
    }
    if (timeout_ > 0) {
      timer_.expires_from_now(boost::posix_time::seconds(timeout_));
      timer_.async_wait(boost::bind(&this_type::handle_timeout,
                                    this_type::shared_from_this(),
                                    boost::arg<1>()));
    }
  }

  void read_status(basic_response<Tag>& response_,
                   boost::asio::streambuf& response_buffer) {
    connection_base::read_status(socket_, response_, response_buffer);
  }

  void read_headers(basic_response<Tag>& response,
                    boost::asio::streambuf& response_buffer) {
    connection_base::read_headers(socket_, response, response_buffer);
  }

  void read_body(basic_response<Tag>& response_,
                 boost::asio::streambuf& response_buffer) {
    connection_base::read_body(socket_, response_, response_buffer);
    typename headers_range<basic_response<Tag> >::type connection_range =
        headers(response_)["Connection"];
    if (version_major == 1 && version_minor == 1 && !boost::empty(connection_range) &&
        boost::iequals(boost::begin(connection_range)->second, "close")) {
      close_socket();
    } else if (version_major == 1 && version_minor == 0) {
      close_socket();
    }
  }

  bool is_open() { return socket_.is_open(); }

  void close_socket() {
    timer_.cancel();
    if (!is_open()) return;
    boost::system::error_code ignored;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
    if (ignored) return;
    socket_.close(ignored);
  }

 private:
  void handle_timeout(boost::system::error_code const& ec) {
    if (!ec) close_socket();
  }

  int timeout_;
  boost::asio::deadline_timer timer_;
  resolver_type& resolver_;
  resolver_function_type resolve_;
  boost::asio::ip::tcp::socket socket_;
};

}  // namespace impl
}  // nmaespace http
}  // namespace network
}  // nmaespace boost

#endif  // BOOST_NETWORK_PROTOCOL_HTTP_IMPL_HTTP_SYNC_CONNECTION_20100
