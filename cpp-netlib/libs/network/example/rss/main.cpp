//            Copyright (c) Glyn Matthews 2011.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "rss.hpp"
#include <boost/network/protocol/http/client.hpp>
#include <boost/foreach.hpp>
#include <iostream>

int main(int argc, char *argv[]) {
  using namespace boost::network;

  if (argc != 2) {
    std::cout << "Usage: " << argv[0] << " <url>" << std::endl;
    return 1;
  }

  try {
    http::client client;
    http::client::request request(argv[1]);
    request << header("Connection", "close");
    http::client::response response = client.get(request);
    rss::channel channel(response);

    std::cout << "Channel: " << channel.title() << " (" << channel.description()
              << ")" << std::endl;
    BOOST_FOREACH(const rss::item & item, channel) {
      std::cout << item.title() << " (" << item.author() << ")" << std::endl;
    }
  }
  catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
