
// Copyright 2010 Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE HTTP Asynchronous Server Tests

#include <vector>

#include <boost/config/warning_disable.hpp>
#include <boost/network/include/http/server.hpp>
#include <boost/network/utils/thread_pool.hpp>
#include <boost/range/algorithm/find_if.hpp>

namespace net = boost::network;
namespace http = boost::network::http;
namespace utils = boost::network::utils;

struct async_hello_world;
typedef http::async_server<async_hello_world> server;

struct async_hello_world {

  struct is_content_length {
    template <class Header>
    bool operator()(Header const& header) {
      return boost::iequals(header.name, "content-length");
    }
  };

  void operator()(server::request const& request,
                  server::connection_ptr connection) {
    static server::response_header headers[] = {
        {"Connection", "close"}, {"Content-Type", "text/plain"},
        {"Server", "cpp-netlib/0.9-devel"}};
    if (request.method == "HEAD") {
      connection->set_status(server::connection::ok);
      connection->set_headers(boost::make_iterator_range(headers, headers + 3));
    } else {
      if (request.method == "PUT" || request.method == "POST") {
        static std::string bad_request("Bad Request.");
        server::request::headers_container_type::iterator found =
            boost::find_if(request.headers, is_content_length());
        if (found == request.headers.end()) {
          connection->set_status(server::connection::bad_request);
          connection->set_headers(
              boost::make_iterator_range(headers, headers + 3));
          connection->write(bad_request);
          return;
        }
      }
      static char const* hello_world = "Hello, World!";
      connection->set_status(server::connection::ok);
      connection->set_headers(boost::make_iterator_range(headers, headers + 3));
      std::vector<boost::asio::const_buffer> iovec;
      iovec.push_back(boost::asio::const_buffer(hello_world, 13));
      connection->write(iovec,
                        boost::bind(&async_hello_world::error, this, _1));
    }
  }

  void error(boost::system::error_code const& ec) {
    // do nothing here.
  }
};

int main(int argc, char* argv[]) {
  utils::thread_pool thread_pool(2);
  async_hello_world handler;
  server instance("127.0.0.1", "8000", handler, thread_pool);
  instance.run();
  return 0;
}
