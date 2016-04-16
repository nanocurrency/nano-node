#ifndef BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_OPTIONS_HPP_20130128
#define BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_OPTIONS_HPP_20130128

#include <boost/network/traits/string.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/optional/optional.hpp>
#include <boost/asio/io_service.hpp>

// Copyright 2013 Google, Inc.
// Copyright 2013 Dean Michael Berris <dberris@google.com>
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

namespace boost {
namespace network {
namespace http {

template <class Tag>
struct client_options {
  typedef typename string<Tag>::type string_type;

  client_options()
      : cache_resolved_(false),
        follow_redirects_(false),
        openssl_certificate_(),
        openssl_verify_path_(),
        openssl_certificate_file_(),
        openssl_private_key_file_(),
        openssl_ciphers_(),
        openssl_options_(0),
        io_service_(),
        always_verify_peer_(false),
        timeout_(0) {}

  client_options(client_options const& other)
      : cache_resolved_(other.cache_resolved_),
        follow_redirects_(other.follow_redirects_),
        openssl_certificate_(other.openssl_certificate_),
        openssl_verify_path_(other.openssl_verify_path_),
        openssl_certificate_file_(other.openssl_certificate_file_),
        openssl_private_key_file_(other.openssl_private_key_file_),
        openssl_ciphers_(other.openssl_ciphers_),
        openssl_options_(other.openssl_options_),
        io_service_(other.io_service_),
        always_verify_peer_(other.always_verify_peer_),
        timeout_(other.timeout_) {}

  client_options& operator=(client_options other) {
    other.swap(*this);
    return *this;
  }

  void swap(client_options& other) {
    using std::swap;
    swap(cache_resolved_, other.cache_resolved_);
    swap(follow_redirects_, other.follow_redirects_);
    swap(openssl_certificate_, other.openssl_certificate_);
    swap(openssl_verify_path_, other.openssl_verify_path_);
    swap(openssl_certificate_file_, other.openssl_certificate_file_);
    swap(openssl_private_key_file_, other.openssl_private_key_file_);
    swap(openssl_ciphers_, other.openssl_ciphers_);
    swap(openssl_options_, other.openssl_options_);
    swap(io_service_, other.io_service_);
    swap(always_verify_peer_, other.always_verify_peer_);
    swap(timeout_, other.timeout_);
  }

  client_options& cache_resolved(bool v) {
    cache_resolved_ = v;
    return *this;
  }

  client_options& follow_redirects(bool v) {
    follow_redirects_ = v;
    return *this;
  }

  client_options& openssl_certificate(string_type const& v) {
    openssl_certificate_ = v;
    return *this;
  }

  client_options& openssl_verify_path(string_type const& v) {
    openssl_verify_path_ = v;
    return *this;
  }

  client_options& openssl_certificate_file(string_type const& v) {
    openssl_certificate_file_ = v;
    return *this;
  }

  client_options& openssl_private_key_file(string_type const& v) {
    openssl_private_key_file_ = v;
    return *this;
  }

  client_options& openssl_ciphers(string_type const& v) {
    openssl_ciphers_ = v;
    return *this;
  }

  client_options& openssl_options(long o) {
    openssl_options_ = o;
    return *this;
  }

  client_options& io_service(boost::shared_ptr<boost::asio::io_service> v) {
    io_service_ = v;
    return *this;
  }

  client_options& always_verify_peer(bool v) {
    always_verify_peer_ = v;
    return *this;
  }

  client_options& timeout(int v) {
    timeout_ = v;
    return *this;
  }

  bool cache_resolved() const { return cache_resolved_; }

  bool follow_redirects() const { return follow_redirects_; }

  boost::optional<string_type> openssl_certificate() const {
    return openssl_certificate_;
  }

  boost::optional<string_type> openssl_verify_path() const {
    return openssl_verify_path_;
  }

  boost::optional<string_type> openssl_certificate_file() const {
    return openssl_certificate_file_;
  }

  boost::optional<string_type> openssl_private_key_file() const {
    return openssl_private_key_file_;
  }

  boost::optional<string_type> openssl_ciphers() const {
    return openssl_ciphers_;
  }

  long openssl_options() const { return openssl_options_; }

  boost::shared_ptr<boost::asio::io_service> io_service() const {
    return io_service_;
  }

  bool always_verify_peer() const { return always_verify_peer_; }

  int timeout() const { return timeout_; }

 private:
  bool cache_resolved_;
  bool follow_redirects_;
  boost::optional<string_type> openssl_certificate_;
  boost::optional<string_type> openssl_verify_path_;
  boost::optional<string_type> openssl_certificate_file_;
  boost::optional<string_type> openssl_private_key_file_;
  boost::optional<string_type> openssl_ciphers_;
  long openssl_options_;
  boost::shared_ptr<boost::asio::io_service> io_service_;
  bool always_verify_peer_;
  int timeout_;
};

template <class Tag>
inline void swap(client_options<Tag>& a, client_options<Tag>& b) {
  a.swap(b);
}

} /* http */
} /* network */
} /* boost */

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_OPTIONS_HPP_20130128 */
