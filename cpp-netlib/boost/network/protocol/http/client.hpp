#ifndef BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_20091215
#define BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_20091215

//          Copyright Dean Michael Berris 2007-2010.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <boost/network/version.hpp>
#include <boost/network/traits/ostringstream.hpp>
#include <boost/network/protocol/http/message.hpp>
#include <boost/network/protocol/http/response.hpp>
#include <boost/network/protocol/http/request.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/foreach.hpp>
#include <ostream>
#include <istream>
#include <string>
#include <stdexcept>
#include <map>

#include <boost/network/protocol/http/client/facade.hpp>
#include <boost/network/protocol/http/client/macros.hpp>
#include <boost/network/protocol/http/client/options.hpp>

namespace boost {
namespace network {
namespace http {

template <class Tag, unsigned version_major, unsigned version_minor>
struct basic_client : basic_client_facade<Tag, version_major, version_minor> {
 private:
  typedef basic_client_facade<Tag, version_major, version_minor>
      base_facade_type;

 public:
  typedef basic_request<Tag> request;
  typedef basic_response<Tag> response;
  typedef typename string<Tag>::type string_type;
  typedef Tag tag_type;
  typedef client_options<Tag> options;

  // Constructors
  // =================================================================
  // This constructor takes a single options argument of type
  // client_options. See boost/network/protocol/http/client/options.hpp
  // for more details.
  explicit basic_client(options const& options) : base_facade_type(options) {}

  // This default constructor sets up the default options.
  basic_client() : base_facade_type(options()) {}
  //
  // =================================================================
};

#ifndef BOOST_NETWORK_HTTP_CLIENT_DEFAULT_TAG
#define BOOST_NETWORK_HTTP_CLIENT_DEFAULT_TAG tags::http_async_8bit_udp_resolve
#endif

typedef basic_client<BOOST_NETWORK_HTTP_CLIENT_DEFAULT_TAG, 1, 1> client;

}  // namespace http

}  // namespace network

}  // namespace boost

#endif  // BOOST_NETWORK_PROTOCOL_HTTP_CLIENT_20091215
