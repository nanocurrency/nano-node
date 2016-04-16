// Copyright 2014 Dean Michael Berris <dberris@google.com>
// Copyright 2014 Google, Inc.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <iostream>

#include <boost/network/include/http/client.hpp>

namespace http = boost::network::http;

int main(int, char * []) {
  http::client client;
  http::client::request request("https://www.google.com/");
  http::client::response response = client.get(request);
  std::cout << body(response) << std::endl;
}
