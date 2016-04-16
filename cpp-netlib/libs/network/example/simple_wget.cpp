//            Copyright (c) Glyn Matthews 2009, 2010.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

//[ simple_wget_main
/*`
  This is a very basic clone of wget.  It's missing a lot of
  features, such as content-type detection, but it does the
  fundamental things the same.

  It demonstrates the use the `uri` and the `http::client`.
*/

#include <boost/network/protocol/http/client.hpp>
#include <boost/network/uri.hpp>
#include <string>
#include <fstream>
#include <iostream>

namespace http = boost::network::http;
namespace uri = boost::network::uri;

namespace {
std::string get_filename(const uri::uri &url) {
  std::string path = uri::path(url);
  std::size_t index = path.find_last_of('/');
  std::string filename = path.substr(index + 1);
  return filename.empty() ? "index.html" : filename;
}
}  // namespace

int main(int argc, char *argv[]) {

  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " url" << std::endl;
    return 1;
  }

  http::client client;
  try {
    http::client::request request(argv[1]);
    http::client::response response = client.get(request);

    std::string filename = get_filename(request.uri());
    std::cout << "Saving to: " << filename << std::endl;
    std::ofstream ofs(filename.c_str());
    ofs << static_cast<std::string>(body(response)) << std::endl;
  }
  catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }

  return 0;
}
//]
