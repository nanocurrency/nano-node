
// Copyright 2010 Dean Michael Berris.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_MODULE HTTP Asynchronous Server Tests

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
      return boost::iequals(name(header), "content-length");
    }
  };

  void operator()(server::request const& request,
                  server::connection_ptr connection) {
    static server::response_header headers[] = {{"Connection", "close"},
                                                {"Content-Type", "text/plain"},
                                                {"Server", "cpp-netlib/0.9"},
                                                {"Content-Length", "13"}};
    static std::string hello_world("Hello, World!");
    connection->set_status(server::connection::ok);
    connection->set_headers(boost::make_iterator_range(headers, headers + 4));
    connection->write(hello_world);
  }
};

int main(int argc, char* argv[]) {
  utils::thread_pool thread_pool(2);
  async_hello_world handler;
  std::string port = "8000";
  if (argc > 1) port = argv[1];
  server instance("localhost", port, handler, thread_pool,
                  http::_reuse_address = true);
  instance.run();
  return 0;
}
