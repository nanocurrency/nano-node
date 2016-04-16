
// Copyright Dean Michael Berris 2010.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

//[ http_one_liner_main
/*`

 */
#include <boost/network/protocol/http.hpp>

using namespace std;
using namespace boost::network;
using namespace boost::network::http;

int main(int argc, const char *argv[]) {
  /*<< The client sends an HTTP request to the server, and the output
       is printed to the console. >>*/
  cout << body(client().get(client::request("http://www.boost.org/")));
  return 0;
}
