#ifndef BOOST_NETWORK_PROTOCOL_HTTP_REQUEST_CONCEPT_HPP_20100603
#define BOOST_NETWORK_PROTOCOL_HTTP_REQUEST_CONCEPT_HPP_20100603

// Copyright 2010 (c) Dean Michael Berris.
// Copyright 2010 (c) Sinefunc, Inc.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/concept_check.hpp>
#include <boost/network/message/message_concept.hpp>
#include <boost/cstdint.hpp>
#include <boost/mpl/if.hpp>
#include <boost/type_traits/is_base_of.hpp>

namespace boost {
namespace network {
namespace http {

template <class R>
struct ServerRequest {
  typedef typename R::string_type string_type;
  typedef typename R::tag tag;
  typedef typename R::headers_container_type headers_container_type;

  BOOST_CONCEPT_USAGE(ServerRequest) {
    string_type source_, method_, destination_;
    boost::uint8_t major_version_, minor_version_;
    headers_container_type headers_;
    string_type body_;

    source_ = source(request);
    method_ = method(request);
    destination_ = destination(request);
    major_version_ = major_version(request);
    minor_version_ = minor_version(request);
    headers_ = headers(request);
    body_ = body(request);

    source(request, source_);
    method(request, method_);
    destination(request, destination_);
    major_version(request, major_version_);
    minor_version(request, minor_version_);
    headers(request, headers_);
    add_header(request, string_type(), string_type());
    remove_header(request, string_type());
    clear_headers(request);
    body(request, body_);

    string_type name, value;

    request << ::boost::network::source(source_)
            << ::boost::network::destination(destination_)
            << ::boost::network::http::method(method_)
            << ::boost::network::http::major_version(major_version_)
            << ::boost::network::http::minor_version(minor_version_)
            << ::boost::network::header(name, value)
            << ::boost::network::body(body_);

    (void)source_;
    (void)method_;
    (void)destination_;
    (void)major_version_;
    (void)minor_version_;
    (void)headers_;
    (void)body_;
    (void)name;
    (void)value;
  }

 private:
  R request;
};

template <class R>
struct ClientRequest : boost::network::Message<R> {
  typedef typename R::string_type string_type;
  typedef typename R::port_type port_type;

  BOOST_CONCEPT_USAGE(ClientRequest) {
    string_type tmp;
    R request_(tmp);
    swap(request, request_);  // swappable via ADL

    string_type host_ = host(request);
    port_type port_ = port(request);
    string_type path_ = path(request);
    string_type query_ = query(request);
    string_type anchor_ = anchor(request);
    string_type protocol_ = protocol(request);

    request << uri(string_type());

    boost::network::http::uri(request, string_type());

    (void)host_;
    (void)port_;
    (void)path_;
    (void)query_;
    (void)anchor_;
    (void)protocol_;
  }

 private:
  R request;
};

}  // namespace http

}  // namespace network

}  // namespace boost

#endif  // BOOST_NETWORK_PROTOCOL_HTTP_REQUEST_CONCEPT_HPP_20100603
