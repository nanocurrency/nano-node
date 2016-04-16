#ifndef BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_FACADE_HPP_20100623
#define BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_FACADE_HPP_20100623

// Copyright 2013 Google, Inc.
// Copyright 2010 Dean Michael Berris <dberris@google.com>
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/network/protocol/http/request.hpp>
#include <boost/network/protocol/http/response.hpp>
#include <boost/network/protocol/http/client/pimpl.hpp>
#include <boost/network/protocol/http/client/options.hpp>

namespace boost {
namespace network {
namespace http {

template <class Tag>
struct basic_request;

template <class Tag>
struct basic_response;

template <class Tag, unsigned version_major, unsigned version_minor>
struct basic_client_facade {

  typedef typename string<Tag>::type string_type;
  typedef basic_request<Tag> request;
  typedef basic_response<Tag> response;
  typedef basic_client_impl<Tag, version_major, version_minor> pimpl_type;
  typedef function<void(iterator_range<char const*> const&,
                        system::error_code const&)> body_callback_function_type;
  typedef function<bool(string_type&)> body_generator_function_type;

  explicit basic_client_facade(client_options<Tag> const& options) {
    init_pimpl(options);
  }

  ~basic_client_facade() { pimpl->wait_complete(); }

  response head(request const& request) {
    return pimpl->request_skeleton(request, "HEAD", false,
                                   body_callback_function_type(),
                                   body_generator_function_type());
  }

  response get(request const& request,
               body_callback_function_type body_handler =
                   body_callback_function_type()) {
    return pimpl->request_skeleton(request, "GET", true, body_handler,
                                   body_generator_function_type());
  }

  response post(request request, string_type const& body = string_type(),
                string_type const& content_type = string_type(),
                body_callback_function_type body_handler =
                    body_callback_function_type(),
                body_generator_function_type body_generator =
                    body_generator_function_type()) {
    if (body != string_type()) {
      request << remove_header("Content-Length")
              << header("Content-Length",
                        boost::lexical_cast<string_type>(body.size()))
              << boost::network::body(body);
    }
    typename headers_range<basic_request<Tag> >::type content_type_headers =
        headers(request)["Content-Type"];
    if (content_type != string_type()) {
      if (!boost::empty(content_type_headers))
        request << remove_header("Content-Type");
      request << header("Content-Type", content_type);
    } else {
      if (boost::empty(content_type_headers)) {
        typedef typename char_<Tag>::type char_type;
        static char_type content_type[] = "x-application/octet-stream";
        request << header("Content-Type", content_type);
      }
    }
    return pimpl->request_skeleton(request, "POST", true, body_handler,
                                   body_generator);
  }

  response post(request const& request,
                body_generator_function_type body_generator,
                body_callback_function_type callback =
                    body_generator_function_type()) {
    return pimpl->request_skeleton(request, "POST", true, callback,
                                   body_generator);
  }

  response post(request const& request, body_callback_function_type callback,
                body_generator_function_type body_generator =
                    body_generator_function_type()) {
    return post(request, string_type(), string_type(), callback,
                body_generator);
  }

  response post(request const& request, string_type const& body,
                body_callback_function_type callback,
                body_generator_function_type body_generator =
                    body_generator_function_type()) {
    return post(request, body, string_type(), callback, body_generator);
  }

  response put(request request, string_type const& body = string_type(),
               string_type const& content_type = string_type(),
               body_callback_function_type body_handler =
                   body_callback_function_type(),
               body_generator_function_type body_generator =
                   body_generator_function_type()) {
    if (body != string_type()) {
      request << remove_header("Content-Length")
              << header("Content-Length",
                        boost::lexical_cast<string_type>(body.size()))
              << boost::network::body(body);
    }
    typename headers_range<basic_request<Tag> >::type content_type_headers =
        headers(request)["Content-Type"];
    if (content_type != string_type()) {
      if (!boost::empty(content_type_headers))
        request << remove_header("Content-Type");
      request << header("Content-Type", content_type);
    } else {
      if (boost::empty(content_type_headers)) {
        typedef typename char_<Tag>::type char_type;
        static char_type content_type[] = "x-application/octet-stream";
        request << header("Content-Type", content_type);
      }
    }
    return pimpl->request_skeleton(request, "PUT", true, body_handler,
                                   body_generator);
  }

  response put(request const& request, body_callback_function_type callback,
               body_generator_function_type body_generator =
                   body_generator_function_type()) {
    return put(request, string_type(), string_type(), callback, body_generator);
  }

  response put(request const& request, string_type body,
               body_callback_function_type callback,
               body_generator_function_type body_generator =
                   body_generator_function_type()) {
    return put(request, body, string_type(), callback, body_generator);
  }

  response delete_(request const& request,
                   body_callback_function_type body_handler =
                       body_callback_function_type()) {
    return pimpl->request_skeleton(request, "DELETE", true, body_handler,
                                   body_generator_function_type());
  }

  void clear_resolved_cache() { pimpl->clear_resolved_cache(); }

 protected:
  boost::shared_ptr<pimpl_type> pimpl;

  void init_pimpl(client_options<Tag> const& options) {
    pimpl.reset(new pimpl_type(
        options.cache_resolved(), options.follow_redirects(),
        options.always_verify_peer(), options.openssl_certificate(),
        options.openssl_verify_path(), options.openssl_certificate_file(),
        options.openssl_private_key_file(), options.openssl_ciphers(),
        options.openssl_options(), options.io_service(), options.timeout()));
  }
};

}  // namespace http
}  // namespace network
}  // namespace boost

#endif  // BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_FACADE_HPP_20100623
