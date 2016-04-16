#ifndef BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_ASYNC_IMPL_HPP_20100623
#define BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_ASYNC_IMPL_HPP_20100623

// Copyright Dean Michael Berris 2010.
// Copyright 2011 Dean Michael Berris (dberris@google.com).
// Copyright 2011 Google, Inc.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>
#include <boost/thread/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

namespace boost {
namespace network {
namespace http {

template <class Tag, unsigned version_major, unsigned version_minor>
struct basic_client_impl;

namespace impl {
template <class Tag, unsigned version_major, unsigned version_minor>
struct async_client
    : connection_policy<Tag, version_major, version_minor>::type {
  typedef typename connection_policy<Tag, version_major, version_minor>::type
      connection_base;
  typedef typename resolver<Tag>::type resolver_type;
  typedef typename string<Tag>::type string_type;

  typedef function<void(boost::iterator_range<char const*> const&,
                        system::error_code const&)> body_callback_function_type;

  typedef function<bool(string_type&)> body_generator_function_type;

  async_client(bool cache_resolved, bool follow_redirect,
               bool always_verify_peer, int timeout,
               boost::shared_ptr<boost::asio::io_service> service,
               optional<string_type> const& certificate_filename,
               optional<string_type> const& verify_path,
               optional<string_type> const& certificate_file,
               optional<string_type> const& private_key_file,
               optional<string_type> const& ciphers, long ssl_options)
      : connection_base(cache_resolved, follow_redirect, timeout),
        service_ptr(service.get()
                        ? service
                        : boost::make_shared<boost::asio::io_service>()),
        service_(*service_ptr),
        resolver_(service_),
        sentinel_(new boost::asio::io_service::work(service_)),
        certificate_filename_(certificate_filename),
        verify_path_(verify_path),
        certificate_file_(certificate_file),
        private_key_file_(private_key_file),
        ciphers_(ciphers),
        ssl_options_(ssl_options),
        always_verify_peer_(always_verify_peer) {
    connection_base::resolver_strand_.reset(
        new boost::asio::io_service::strand(service_));
    if (!service)
      lifetime_thread_.reset(new boost::thread(
          boost::bind(&boost::asio::io_service::run, &service_)));
  }

  ~async_client() throw() { sentinel_.reset(); }

  void wait_complete() {
    sentinel_.reset();
    if (lifetime_thread_.get()) {
      lifetime_thread_->join();
      lifetime_thread_.reset();
    }
  }

  basic_response<Tag> const request_skeleton(
      basic_request<Tag> const& request_, string_type const& method,
      bool get_body, body_callback_function_type callback,
      body_generator_function_type generator) {
    typename connection_base::connection_ptr connection_;
    connection_ = connection_base::get_connection(
        resolver_, request_, always_verify_peer_, certificate_filename_,
        verify_path_, certificate_file_, private_key_file_, ciphers_,
        ssl_options_);
    return connection_->send_request(method, request_, get_body, callback,
                                     generator);
  }

  boost::shared_ptr<boost::asio::io_service> service_ptr;
  boost::asio::io_service& service_;
  resolver_type resolver_;
  boost::shared_ptr<boost::asio::io_service::work> sentinel_;
  boost::shared_ptr<boost::thread> lifetime_thread_;
  optional<string_type> certificate_filename_;
  optional<string_type> verify_path_;
  optional<string_type> certificate_file_;
  optional<string_type> private_key_file_;
  optional<string_type> ciphers_;
  long ssl_options_;
  bool always_verify_peer_;
};
}  // namespace impl
}  // namespace http
}  // namespace network
}  // namespace boost

#endif  // BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_ASYNC_IMPL_HPP_20100623
