
// Copyright 2008, 2014 Dean Michael Berris <dberris@google.com>
// Copyright 2014 Google, Inc.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

//[ http_client_main
/*`
  This application takes a URL as a command line argument and prints
  the resource to the console.
*/
#include <boost/program_options.hpp>
#include <boost/network/protocol/http.hpp>
#include <string>
#include <utility>
#include <iostream>

namespace po = boost::program_options;

int main(int argc, char* argv[]) {
  using namespace boost::network;
  po::options_description options("Allowed options");
  std::string output_filename, source;
  bool show_headers;
  options.add_options()("help,h", "produce help message")(
      "headers,H", "print headers")("status,S", "print status and message")(
      "source,s", po::value<std::string>(&source), "source URL");

  po::positional_options_description positional_options;
  positional_options.add("source", 1);
  po::variables_map vm;
  try {
    po::store(po::command_line_parser(argc, argv)
                  .options(options)
                  .positional(positional_options)
                  .run(),
              vm);
    po::notify(vm);
  }
  catch (std::exception& e) {
    std::cout << "Error: " << e.what() << std::endl;
    std::cout << options << std::endl;
    return EXIT_FAILURE;
  }

  if (vm.count("help")) {
    std::cout << options << std::endl;
    return EXIT_SUCCESS;
  }

  if (vm.count("source") < 1) {
    std::cout << "Error: Source URL required." << std::endl;
    std::cout << options << std::endl;
    return EXIT_FAILURE;
  }

  show_headers = vm.count("headers") ? true : false;
  bool show_status = vm.count("status") ? true : false;

  http::client::request request(source);
  http::client::string_type destination_ = host(request);

  request << ::boost::network::header("Connection", "close");
  http::client::options client_options;
  client_options.follow_redirects(true);
  http::client client(client_options);
  http::client::response response = client.get(request);

  if (show_status)
    std::cout << status(response) << " " << status_message(response)
              << std::endl;

  if (show_headers) {
    headers_range<http::client::response>::type headers_ = response.headers();
    typedef std::pair<std::string, std::string> header_type;
    BOOST_FOREACH(header_type const & header, headers_) {
      std::cout << header.first << ": " << header.second << std::endl;
    }
    std::cout << std::endl;
  }

  std::cout << body(response);
  return EXIT_SUCCESS;
}
//]
