// Copyright 2009 (c) Tarro, Inc.
// Copyright 2009-2010 (c) Dean Michael Berris <mikhailberis@gmail.com>
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <boost/config/warning_disable.hpp>
#include <boost/network/protocol/http/server.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/lexical_cast.hpp>
#include <string>
#include <iostream>

namespace http = boost::network::http;
using boost::assign::list_of;
using boost::lexical_cast;
using std::string;
using std::cerr;
using std::endl;

struct hello_world;
typedef http::server<hello_world> server;

struct hello_world {

  void operator()(server::request const& request, server::response& response) {
    static server::response::header_type header = {"Connection", "close"};
    response =
        server::response::stock_reply(server::response::ok, "Hello, World!");
    response.headers.push_back(header);
    assert(response.status == server::response::ok);
    assert(response.headers.size() == 3);
    assert(response.content == "Hello, World!");
  }

  void log(string const& data) {
    cerr << data << endl;
    abort();
  }
};

int main(int argc, char* argv[]) {
  hello_world handler;
  std::string port = "8000";
  if (argc > 1) port = argv[1];
  server server_("127.0.0.1", port, handler, http::_reuse_address = true);
  server_.run();
  return EXIT_SUCCESS;
}
